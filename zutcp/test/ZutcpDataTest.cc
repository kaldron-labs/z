//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZtArray.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"

using namespace ZuTestUtil;

namespace Zutcp {
struct TCPStateTest {
  template <typename T> static auto &core(T &link) {
    return static_cast<typename T::Base &>(link);
  }
  template <typename T> static void stage(T &link, bool sack = true, bool reset = true) {
    auto &c = core(link);
    if (reset) c.resetLink();
    c.configLocalParams(link.app());
    if (c.isServer())
      c.initServerPathTx({ZiIP{"198.18.0.1"}, 18191}, {ZiIP{"198.18.0.2"}, 18192}, 1200);
    else
      c.initClientPathTx({ZiIP{"198.18.0.1"}, 18191}, {ZiIP{"198.18.0.2"}, 18192}, 1200);
    c.m_closedKeyRx = ClosedTupleKey{ZiSockAddr{ZiIP{"198.18.0.1"}, 18191},
      ZiSockAddr{ZiIP{"198.18.0.2"}, 18192}};
    c.m_txOrigins = c.m_rxOrigins = {1000, 2000, true, true, c.isServer()};
    c.startHandshake();
    c.establish();
    c.m_rxSynAcked = c.m_txSynAcked = c.m_tcpSynSent = true;
    c.m_rxOptionsSet = c.m_txOptionsSet = true;
    c.m_txTCPState.flags = TCPOptions::MSS |
      (sack ? TCPOptionState::SACKReceive | TCPOptionState::SACKSend : 0);
    c.m_rxTCPState.flags = sack ? TCPOptionState::SACKSend : 0;
    c.m_txTCPState.peerMSS = 1200;
    c.m_txWindowSet = true;
    c.txCredit(65535);
    c.publishWriteMPS();
  }
  template <typename T> static bool record(T &link, const SentFrameRef &ref) {
    auto &c = core(link);
    auto id = c.m_txID++;
    if (!c.recordTxPkt(id, unsigned(ref.length) + unsigned(ref.fin), ref, {}, EcnMark::NotECT))
      return false;
    c.m_txPkts.acceptedData(id, c.transportNow());
    c.publishTxCommit(ref, false);
    c.acceptTxData(ref);
    if (c.m_txBytes < ref.offset + ref.length) c.m_txBytes = ref.offset + ref.length;
    c.m_txDataCredit.consumeTo(c.m_txBytes);
    if (ref.fin) c.m_fin = c.m_finDequeued = true;
    return true;
  }
  template <typename T> static void ack(T &link, uint64_t next, TCPOptions options = {}) {
    core(link).applyTCP({.seq = 0, .ack = uint32_t(next), .window = 65535,
      .flags = 0x10, .options = options});
  }
  template <typename T> static void peer(T &link, uint32_t ack,
      uint16_t window = 65535, uint32_t seq = 0, TCPOptions options = {},
      uint8_t flags = 0x10) {
    core(link).receiveTCP(TCPHeader{.seq = seq, .ack = ack,
      .window = window, .flags = flags, .options = options});
  }
  template <typename T> static bool peerPending(T &link) {
    return core(link).m_peerAckRequest.load_() & 1;
  }
  template <typename T> static void peerNotify(T &link) {
    auto &c = core(link); c.notifyPeerAck(c.m_rxDataGen);
  }
  template <typename T> static void peerWriting(T &link, bool begin) {
    auto &c = core(link);
    if (begin) c.m_peerAckView.version = c.m_peerAckView.version.load_() + 1;
    else c.m_peerAckView.version = c.m_peerAckView.version.load_() - 1;
  }
  template <typename T> static uint64_t peerLimit(T &link) {
    return core(link).m_txDataCredit.limit();
  }
  template <typename T> static unsigned duplicates(T &link) {
    return core(link).m_txSACKDup;
  }
  template <typename T> static bool peerSnapshot(T &link, TCPHeader &header) {
    auto &c = core(link); uint64_t serial;
    return c.m_peerAckView.read(c.m_rxDataGen, serial, header);
  }
  template <typename T> static void peerWrap(T &link, uint64_t next) {
    auto &c = core(link);
    c.m_txAckd = c.m_txNext = c.m_txBytes = c.m_rxPeerAck = next;
    c.m_txRead.next = c.m_txRead.ack = next;
    c.txCredit(next + 65535);
  }
  template <typename T> static unsigned dsackReports(T &link) {
    return core(link).m_txDSACKReports;
  }
  template <typename T> static bool finDone(T &link) {
    auto &c = core(link); return c.m_finAckd && c.m_txCompleted;
  }
  template <typename T> static bool queuedFIN(T &link) {
    auto &c = core(link);
    return c.m_rxState.complete() && c.m_rxDelivered < c.m_rxState.finalSize() &&
      c.rxByteNext() == c.m_rxState.finalSize() + 1;
  }
  template <typename T> static unsigned records(T &link) { return core(link).m_txPkts.count(); }
  template <typename T> static uint64_t packetFlight(T &link) {
    return core(link).m_txPkts.bytesInFlight();
  }
  template <typename T> static uint64_t flight(T &link) {
    return core(link).m_congestion.bytesInFlight();
  }
  template <typename T> static uint64_t discounted(T &link) {
    return core(link).m_txPkts.sackedFlightBytes();
  }
  template <typename T> static uint64_t latest(T &link, const SentFrameRef &ref) {
    return core(link).m_txPkts.transmission(ref).pn;
  }
  template <typename T, typename Send> static void probe(T &link, Send send) {
    core(link).attemptTLP(ZuMv(send));
  }
  template <typename T> static void requestACK(T &link, bool hold) {
    core(link).requestTCPAck(hold);
  }
  template <typename T, typename Send> static bool flush(T &link, Send send) {
    return core(link).flushTCP([&link, &send](const SentFrameRef &ref,
        bool, unsigned, bool) { return link.output(ref, send); });
  }
  template <typename T> static bool heldACK(T &link) { return core(link).m_ackHoldPending; }
  template <typename T> static bool pendingACK(T &link) { return core(link).m_ackPending; }
  template <typename T> static bool postedACK(T &link) {
    return core(link).m_ackRequest.load_() & 1;
  }
  template <typename T> static void advanceRx(T &link, uint64_t next) {
    core(link).m_rxState.delivered(next);
    core(link).publishTCPEcho();
  }
  template <typename T> static void rxMSS(T &link, unsigned mss) {
    core(link).m_rxRoutePayload = mss;
  }
  template <typename T> static ZuTime ackDeadline(T &link) {
    return core(link).m_ackDelayTimerOut;
  }
  template <typename T> static void expireACK(T &link) {
    core(link).cxnTimer(CxnTimer::AckDelay);
  }
  template <typename T> static bool failedBudget(T &link) { return core(link).m_tcpBudgetFailed; }
  template <typename T> static void reset(T &link) { core(link).resetLink(); }
  template <typename T> static void peerTimers(T &link) {
    auto &c = core(link);
    c.startIdleTimerTx(ZuTime{20});
    c.startHeartBeatTimerTx(c.app()->heartBeat());
    c.m_idleAckElicitingSent = true;
  }
  template <typename T> static bool peerTimersAt(T &link, ZuTime base, bool sent) {
    auto &c = core(link);
    return c.m_idleBase == base && c.m_heartBeatBase == base &&
      c.m_idleAckElicitingSent == sent && c.m_idleTimer && c.m_heartBeatTimer &&
      c.m_idleTimerOut == base + ZuTime{20} &&
      c.m_heartBeatTimerOut == base + c.app()->heartBeat();
  }
  template <typename T> static uint64_t echoSerial(T &link) {
    return core(link).m_txRxSerial;
  }
  template <typename T> static bool echoCurrent(T &link) {
    auto &c = core(link);
    return c.refreshTCPRx() && c.m_txRxNext == c.rxByteNext() &&
      c.m_txRxLimit == c.m_rxDataCredit.limit() && c.m_txEchoECE == c.m_rxECE;
  }
  template <typename T> static void staleEcho(T &link, bool stale) {
    auto &c = core(link);
    c.m_txDataGen = c.m_rxDataGen + unsigned(stale);
  }
  template <typename T> static void syn(T &link) {
    auto &c = core(link);
    c.resetLink();
    c.configLocalParams(link.app());
    c.initClientPathTx({ZiIP{"198.18.0.1"}, 18191}, {ZiIP{"198.18.0.2"}, 18192}, 1200);
    c.m_txOrigins = c.m_rxOrigins = {1000, 0, true, false, false};
    c.startHandshake();
    c.txCredit(65535);
    c.m_txLocalTCPOptions.flags = TCPOptions::MSS | TCPOptions::TFO;
    c.m_txLocalTCPOptions.mss = 1200;
    c.m_txLocalTCPOptions.tfoLength = 16;
    memset(c.m_txLocalTCPOptions.tfoCookie, 0x31, 16);
  }
  template <typename T> static void handshake(T &link, bool server) {
    syn(link);
    auto &c = core(link);
    c.m_rxOrigins = c.m_txOrigins = {1000, 2000, true, true, server};
    c.m_txLocalTCPOptions.flags = TCPOptions::MSS;
  }
  template <typename T> static ZuTime handshakeRTT(T &link) { return core(link).m_rtt.latest(); }
  template <typename T> static bool confirmed(T &link) { return core(link).m_txSynAcked; }
  template <typename T> static bool rxConfirmed(T &link) { return core(link).m_rxSynAcked; }
  template <typename T> static bool handshakeACK(T &link, uint32_t ack, bool reset = false) {
    auto &c = core(link);
    return c.qualifyHandshake(uint32_t(c.m_rxOrigins.local + 1 + ack), reset);
  }
  template <typename T> static uint64_t synEnd(T &link) {
    return core(link).m_txRead.synEnd.load_();
  }

  template <typename T> static void synTimeout(T &link) { core(link).synTimeout(); }
  template <typename T> static void fallback(T &link) { core(link).fallbackTFO(); }
  template <typename T> static bool retry(T &link, SentFrameRef &ref) {
    return core(link).nextRetransmit(ref);
  }
  template <typename T> static void timestamps(T &link, uint32_t recent) {
    auto &c = core(link);
    c.m_rxTCPState.flags |= TCPOptions::TS;
    c.m_rxTSKnown = true;
    c.m_rxTSRecent = recent;
    c.m_rxTSRecentTime = c.transportNow();
  }
  template <typename T> static TCPOptions rxOptions(T &link) { return core(link).rxTCPOptions(); }
  template <typename T> static TCPHeader header(T &link) { return core(link).tcpHeader(); }
  template <typename T> static TCPOptions sampledOptions(T &link) {
    auto &c = core(link);
    return c.refreshTCPRx() ? c.m_txRxOptions : TCPOptions{};
  }
  template <typename T> static void pathReceived(T &link, ZiSockAddr peer, unsigned bytes) {
    core(link).recordPathRx(peer, bytes);
  }
  template <typename T> static uint64_t pathBytes(T &link) {
    return core(link).pathDiag_().bytesRx;
  }
  template <typename T> static uint32_t recent(T &link) { return core(link).m_rxTSRecent; }
  template <typename T> static bool frontiers(T &link,
      uint64_t &next, uint64_t &ack, uint64_t &sent) {
    return core(link).readTxView(next, ack, sent);
  }
  template <typename T> static bool learnTimestamp(T &link, unsigned seq, unsigned value) {
    auto &c = core(link);
    return c.timestampReceived({.seq = seq, .flags = 0x10,
      .options = {.flags = TCPOptions::TS, .tsval = value}}, c.transportNow());
  }
  template <typename T> static ZuTime recentTime(T &link) { return core(link).m_rxTSRecentTime; }
  template <typename T> static uint64_t dsacks(T &link) { return core(link).m_txDSACKReports; }
  template <typename T> static void expireRTO(T &link) {
    auto &c = core(link);
    c.app()->clock = c.m_dataRto.deadline();
    c.dataRtoExpired();
  }
  template <typename T> static unsigned mss(T &link, unsigned peer, bool sack, bool v6) {
    auto &c = core(link);
    if (v6) c.initClientPathTx({ZiIP{"2001:db8::1"}, 18191},
      {ZiIP{"2001:db8::2"}, 18192}, 1400);
    c.m_txPublishedMPS = 0;
    c.m_writeMPS = unsigned(-1);
    c.m_txTCPState.flags = (peer ? TCPOptions::MSS : 0) |
      (sack ? TCPOptionState::SACKSend : 0);
    c.m_txTCPState.peerMSS = peer;
    c.m_path.routePayload(1400);
    c.m_path.configuredMaxPayload(1400);
    c.m_path.useRoutePayload();
    return c.publishWriteMPS() ? c.dataMPS() : 0;
  }
  template <typename T> static bool cap(T &link, unsigned cap) {
    auto &c = core(link);
    c.m_path.routePayload(cap);
    return c.publishWriteMPS();
  }
  template <typename T> static void disableSACK(T &link) {
    core(link).m_txTCPState.flags &= ~TCPOptionState::SACKReceive;
  }
  template <typename T> static void window(T &link, unsigned window) {
    core(link).applyTCP({.seq = 0, .ack = 0, .window = uint16_t(window), .flags = 0x10});
  }
  template <typename T> static bool persisting(T &link) {
    auto &c = core(link);
    return *c.m_persistTimerOut && !*c.m_rtoTimerOut && !*c.m_ptoTimerOut;
  }
  template <typename T> static uint64_t cwnd(T &link) { return core(link).m_congestion.cwnd(); }
  template <typename T> static bool pacingReady(T &link) { return core(link).pacingReady(); }
  template <typename T> static auto diag(T &link) { return core(link).txDiag(); }
  template <typename T> static unsigned backoff(T &link) { return core(link).m_dataRto.backoff(); }
  template <typename T> static ZuTime rtoDeadline(T &link) { return core(link).m_dataRto.deadline(); }
  template <typename T> static uint64_t paced(T &link) {
    return core(link).m_pacer.nextSendTime();
  }
  template <typename T> static void pacingRTT(T &link) {
    core(link).m_rtt.sample(timeUS(10000));
  }
  template <typename T> static void pacingDue(T &link) {
    auto &c = core(link);
    c.app()->clock = c.intervalUS(c.m_pacer.nextSendTime());
  }
  template <typename T> static void ecn(T &link) {
    auto &c = core(link);
    c.m_rxECN = c.m_txECN = true;
  }
  template <typename T> static bool cwr(T &link) { return core(link).m_txCWR; }
  template <typename T> static void ece(T &link) {
    core(link).applyTCP({.seq = 0, .ack = 0, .window = 65535, .flags = 0x50});
  }
  template <typename T, typename Send> static void clientOffer(T &link, Send send) {
    core(link).prepareClientInitial(ZuMv(send));
  }
  template <typename T> static void exhaustCwnd(T &link) {
    auto &c = core(link);
    c.m_congestion.timeout(uint64_t(c.transportNow().microsecs()));
  }
  template <typename T, typename Send> static void rtoAttempt(T &link, Send send) {
    core(link).attemptRTO(ZuMv(send));
  }
  template <typename T> static bool rtoIdle(T &link) {
    auto &c = core(link);
    return !c.m_rtoSending && !c.m_rtoReserved && !c.m_rtoAllowance;
  }
  template <typename T> static bool fireTLP(T &link) {
    auto &c = core(link);
    c.m_rtt.sample(timeUS(10000));
    c.m_tlp.sampledRTT();
    c.armTLP();
    if (!*c.m_tlp.deadline()) return false;
    c.app()->clock = c.m_tlp.deadline();
    c.tlpRecovery();
    return !*c.m_tlp.deadline() && !c.m_tlpSending && !c.m_tlpAllowance &&
      c.m_dataRto.deadline() == c.transportNow() +
	c.m_dataRto.timeout(c.m_rtt, c.app()->rtoParams());
  }
  template <typename T> static bool timestampRecord(T &link, const SentFrameRef &ref,
      bool timestamp, bool repeated, bool karn) {
    auto &c = core(link);
    c.m_txTCPState.flags = timestamp ? TCPOptions::TS : 0;
    c.m_txRttFence = karn ? ref.offset + ref.length : 0;
    TCPHeader header;
    if (timestamp) header.options = {.flags = TCPOptions::TS, .tsval = 7};
    for (unsigned i = 0; i <= unsigned(repeated); ++i) {
      auto id = c.m_txID++;
      if (!c.recordTxPkt(id, unsigned(ref.length), ref, header, EcnMark::NotECT)) return false;
      c.m_txPkts.acceptedData(id, c.transportNow());
      c.sentTCPTx(header);
    }
    c.publishTxCommit(ref, false);
    c.acceptTxData(ref);
    c.m_txBytes = ref.offset + ref.length;
    c.m_txDataCredit.consumeTo(c.m_txBytes);
    return true;
  }
  template <typename T> static bool selectProbe(T &link) {
    auto &c = core(link);
    c.m_rtt.sample(timeUS(10000));
    c.m_tlp.sampledRTT();
    c.armTLP();
    c.schedulePTO_();
    return c.m_tlpTimerSelected && *c.m_ptoTimerOut && *c.m_ptoTimerRes &&
      c.m_ptoTimerRes <= c.m_ptoTimerOut && !*c.m_rtoTimerOut && *c.m_rtoTimerRes;
  }
  template <typename T> static bool rtoSelected(T &link) {
    auto &c = core(link);
    return !c.m_tlpTimerSelected && !*c.m_ptoTimerOut && *c.m_ptoTimerRes &&
      *c.m_rtoTimerOut && *c.m_rtoTimerRes && c.m_rtoTimerRes <= c.m_rtoTimerOut &&
      c.m_rtoTimerOut == c.m_dataRto.deadline();
  }
  template <typename T> static void receiveCycle(T &link) {
    auto &c = core(link);
    c.m_rxDelivered = uint64_t(1) << 32;
    c.m_rxState.delivered(c.m_rxDelivered);
    c.m_rxDataCredit.set(c.m_rxDelivered + 4096);
  }
  template <typename T> static void privateBudget(T &link) {
    core(link).m_txPublishedMPS = 0;
  }
  template <typename T> static uint64_t failures(T &link) { return core(link).m_txDiag.failures; }
  template <typename T> static void fastSetup(T &link) {
    auto &c = core(link);
    c.m_congestion = NewReno{200};
    c.m_txTCPState.flags = TCPOptions::MSS | TCPOptionState::SACKReceive;
    c.m_txTCPState.peerMSS = 200;
    c.m_txPublishedMPS = 0;
    c.publishWriteMPS();
  }
  template <typename T> static bool fastLoss(T &link) {
    auto &c = core(link);
    TCPOptions options;
    options.sackCount = 1; options.sack[0] = {200, 400};
    ack(link, 0, options);
    if (!*c.m_rack.deadline) return false;
    c.app()->clock = c.m_rack.deadline;
    c.app()->mx()->cancel(&c.m_lossTimer);
    c.cxnTimer(CxnTimer::Loss);
    return (c.app()->congestionControl() ?
      c.fastRecoveryReady() && !c.congestionAllowance() :
      !c.fastRecoveryReady() && c.congestionAllowance()) &&
      c.m_txPkts.retransmitPending() == 1;
  }
  template <typename T> static bool fastReady(T &link) { return core(link).fastRecoveryReady(); }
  template <typename T> static bool fastIdle(T &link) {
    auto &c = core(link);
    return !c.m_fastSending && !c.m_fastReserved;
  }
  template <typename T> static bool requeue(T &link, const SentFrameRef &ref) {
    return core(link).requeueRetransmit(ref);
  }
  template <typename T> static void delayRecovery(T &link) {
    auto &c = core(link);
    c.m_rtt.sample(timeUS(10000));
    c.m_pacer.sent(c.nowUS(), 200, c.m_congestion.cwnd(), 10000);
  }
  template <typename T> static void firePace(T &link) {
    auto &c = core(link);
    c.app()->clock = c.intervalUS(c.m_pacer.nextSendTime());
    c.app()->mx()->cancel(&c.m_paceTimer);
    c.cxnTimer(CxnTimer::Pace);
  }
  template <typename T> static void release(T &link) {
    auto &c = core(link);
    c.delTimers();
    c.resetTx_();
    c.closeDataRx();
    c.delTimers();
  }
};
}

struct App : Zutcp::Hub<App> {
  using Base = Zutcp::Hub<App>;
  ZiMultiplex multiplex{ZiMxParams().scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  Zi::IOBufAllocator *rx = nullptr, *control = nullptr, *nativeAllocator = nullptr,
    *nativeAllocator2 = nullptr;
  Zxdp::BufPool *nativePool = nullptr;
  ZmQueue<ZmFn<>> txJobs, rxJobs;
  bool holdTx = false, holdRx = false, allowRx = true, writable = true;
  unsigned demands = 0;
  mutable unsigned clockCalls = 0;
  mutable ZmFn<> clockFn;
  unsigned policy = 0, policyCalls = 0, allowCalls = 0;
  ZuTime clock{Zm::now() + ZuTime{1000}};

  template <typename T> bool earlyData(T *link) {
    ++policyCalls;
    if (policy == 1) link->abortLocalRx(7, [] { });
    return policy != 0;
  }
  template <typename T> bool allowEarlyStream(T *link, uint64_t, bool) {
    ++allowCalls;
    if (policy == 2) link->abortLocalRx(7, [] { });
    return policy != 0;
  }

  App(ZuTime ackHold = ZuTime{0, 25000000}, ZuTime heartBeat = {},
      bool congestionControl = true) {
    rx = ZutcpTest::pktRxAllocator(&multiplex);
    control = multiplex.allocator<Zi::IOBufType::TxTCP, Zutcp::PacketBuf, Zutcp::BufSize>();
    ZmAssert_(Base::init(Zutcp::HubParams{&multiplex}.maxData(4096)
      .timestamps(false).ackHoldTimeout(ackHold).heartBeat(heartBeat)
      .congestionControl(congestionControl)));
    ZmAssert_(multiplex.start());
  }
  ~App() { Base::final(); multiplex.stop(); }
  bool nativeTx() const { return true; }
  bool ioActive() const { return false; } // Controlled owners have no raw Stack/endpoint.
  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
  ZuTime transportNow() const {
    ++clockCalls;
    if (auto fn = ZuMv(clockFn)) fn();
    return clock;
  }
  Zi::IOBufAllocator *dataTxAllocator() const { return Zutcp::inlineAllocator(); }
  bool txWritable(const Zutcp::TxBacking &) const { return writable; }
  Zxdp::BufPool *txPool(Zi::IOBufAllocator *a) const {
    return a && (a == nativeAllocator || a == nativeAllocator2) ? nativePool : nullptr;
  }
  bool retainRx(ZiIOBuf *, ZmRef<Zutcp::RxAccount> &, uint64_t) { ++demands; return allowRx; }
  template <typename F> void txRun(F f) {
    if (holdTx) txJobs.push([f = ZuMv(f)]() mutable { f(); }); else f();
  }
  template <typename F> void txPost(F f) {
    txJobs.push([f = ZuMv(f)]() mutable { f(); });
  }
  template <typename F> void rxRun(F f) {
    if (holdRx) rxJobs.push([f = ZuMv(f)]() mutable { f(); }); else f();
  }
  template <typename F> void txInvoke(F f) { txRun(ZuMv(f)); }
  template <typename O, typename F> void txInvoke(O *, F f) { txRun(ZuMv(f)); }
  void runTx() { while (auto f = txJobs.shift()) f(); }
  void runRx() { while (auto f = rxJobs.shift()) f(); }
};

struct Link : ZmNHeap<Link>, Zutcp::Connection<App, Link, Zutcp::TxBufAlloc<>> {
friend inline ZuStringT<"Zutcp.DataTest.Link"> ZmNHeapID(Link *);
  using Base = Zutcp::Connection<App, Link, Zutcp::TxBufAlloc<>>;
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Base::txNext;
  using Base::txAckd;
  unsigned processed = 0, finishes = 0, completions = 0, resets = 0, queued = 0;
  bool ready = true;
  ZmFn<> dequeued;
  ZmFn<bool()> recovery;
  unsigned synCalls = 0, newCalls = 0, retxCalls = 0;
  uint64_t selectedOffset = Zutcp::U64Null;
  void txDequeued() { if (dequeued) dequeued(); }
  Link(App *app, bool fresh = false) : Base{app, fresh} {
    Zutcp::TCPStateTest::stage(*this, true, !fresh);
  }
  int process(Zutcp::Reader &) { ++processed; return 0; }
  bool applicationReadyRx(bool early = false) const { return early || ready; }
  bool flushTx() { return false; }
  bool flushTx(ZiSockAddr) { return false; }
  void queueTxFlush() { }
  void queueTxFlush(ZiSockAddr) { }
  void queueRetransmit() { ++queued; }
  bool retransmit(bool = false, unsigned = 2) { return recovery ? recovery() : false; }
  void pto() { Base::synTimeout(); }
  bool sendSyn() { ++synCalls; return false; }
  bool sendTLPNew() { ++newCalls; return false; }
  bool sendTLPRetx(Zutcp::SentFrameRef &ref) {
    ++retxCalls; selectedOffset = ref.offset; return false;
  }
  void markEstablished() { }
  bool sendTCPReset() { ++resets; return false; }
  void nativeFinished() {
    ++finishes;
    Base::disconnect_(false, [this] { ++completions; });
  }
  void received_(Zutcp::Datagram d) { Base::receiveSegment(ZuMv(d)); }
  bool receive(Zutcp::Datagram d) { return Base::receiveSegment(ZuMv(d)); }
  void resume() { Base::resumeApplicationRx(Base::dataGen()); }
  bool next(Zutcp::SentFrameRef &ref) { return Base::nextRetransmit(ref); }
  template <typename Send>
  bool output(const Zutcp::SentFrameRef &ref, Send send, bool syn = false) {
    return Base::sendTCP(ref, syn, 0,
      [this]() -> ZmRef<Zutcp::PacketBuf> {
	auto buf = app()->control->allocBuf();
	return ZmRef<Zutcp::PacketBuf>{static_cast<Zutcp::PacketBuf *>(buf.ptr())};
      }, [send = ZuMv(send)](auto buf, auto, const Zxdp::CommitFn &commit) mutable {
	return send(ZuMv(buf), commit);
      });
  }
  template <typename Send>
  bool recoveryOutput(const Zutcp::SentFrameRef &ref, Send send) {
    return Base::sendRecovery(ref, [this, &ref, send = ZuMv(send)]() mutable {
      return output(ref, ZuMv(send));
    });
  }
  void finish() {
    app()->holdTx = app()->holdRx = false;
    app()->runTx(); app()->runRx(); app()->runTx();
    Zutcp::TCPStateTest::release(*this);
  }
};

struct Native : ZmNHeap<Native>, Zi::IOBufAllocator {
friend inline ZuStringT<"Zutcp.DataTest.Native"> ZmNHeapID(Native *);
  using ZmNHeap<Native>::operator new;
  using ZmNHeap<Native>::operator delete;
  alignas(ZiIOBuf_Align) uint8_t bytes[512];
  unsigned returned = 0;
  bool valid = true;
  Native(ZiMultiplex *mx) : IOBufAllocator{mx, Zi::IOBufType::TxTCP, 512, 256} {
    memset(bytes, 0x6b, sizeof(bytes));
  }
  uint8_t *alloc(unsigned) const override { return nullptr; }
  void free(uint8_t *p, unsigned n, bool registered) const override {
    auto self = const_cast<Native *>(this);
    self->valid &= p == bytes && n == sizeof(bytes) && registered;
    ++self->returned;
  }
  ZmRef<ZiIOBuf> allocBuf(void *, uint32_t, uint32_t) override { return {}; }
};

static Zutcp::SentFrameRef retained(Native &a, Zxdp::BufPool &pool,
    unsigned length = 64, uint64_t offset = 0)
{
  ZmRef<Zutcp::TxBacking> source = new Zutcp::TxRanges::Node{};
  source->native = &a;
  source->pool = &pool;
  source->data = a.bytes;
  source->size = sizeof(a.bytes);
  source->payloadOffset = 256;
  source->payloadLength = source->preparedLength = length;
  source->backing.adopt(&a, a.bytes, sizeof(a.bytes));
  return Zutcp::SentFrameRef::data({source, 256, length, offset}, false);
}
static ZmRef<ZiIOBuf> packet(App &app, ZuBSpan bytes)
{
  auto buf = app.rx->allocBuf();
  memcpy(buf->data(), bytes.data(), bytes.length());
  buf->length = bytes.length();
  return buf;
}
static Zutcp::Datagram input(App &app, ZuBSpan bytes, uint32_t seq = 0, uint32_t ack = 0,
    uint8_t flags = 0x10)
{
  Zutcp::Datagram d{bytes ? packet(app, bytes) : ZmRef<ZiIOBuf>{},
    {ZiIP{"198.18.0.2"}, 18192}};
  d.local = {ZiIP{"198.18.0.1"}, 18191};
  d.tcp = {.seq = 2001 + seq, .ack = 1001 + ack, .window = 65535, .flags = flags};
  return d;
}
static unsigned consume(Link &link)
{
  auto n = link.reader().consume(
    [](auto span) -> int64_t { return span.length(); }, [](auto) { });
  return n > 0 ? unsigned(n) : 0;
}

static void peerEcho(unsigned scenario)
{
  ZuTestScopeRT(peerEcho);
  App app{ZuTime{0, 25000000}, ZuTime{10}};
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::peerTimers(*link);
  auto before = app.clock;
  app.holdTx = true;
  bool received = link->receive(input(app, {}));
  ZuCHECK(received && app.txJobs.count_() == 2,
    "pure ACK with active peer timers queues ACK processing and one activity event");
  // Process the ACK, then observe the explicit peer-activity event alone.
  if (auto job = app.txJobs.shift()) job();
  if (scenario == 1) Zutcp::TCPStateTest::staleEcho(*link, true);
  if (scenario == 2) link->disconnecting(true);
  app.clock += ZuTime{1};
  if (auto job = app.txJobs.shift()) job();
  bool timers = Zutcp::TCPStateTest::peerTimersAt(
    *link, scenario == 2 ? before : app.clock, scenario == 2);
  bool echo = Zutcp::TCPStateTest::echoCurrent(*link) == (scenario != 1);
  ZuCHECK(timers && echo && !app.txJobs.count_(),
    "activity refreshes peer timers while the shared view rejects stale-generation reads");
  if (scenario == 1) Zutcp::TCPStateTest::staleEcho(*link, false);
  if (scenario == 2) link->disconnecting(false);
  link->finish();
}

static void wholeOriginal(bool accept, bool syn)
{
  ZuTestScopeRT(wholeOriginal);
  App app;
  ZmRef<Link> link = new Link{&app};
  if (syn) Zutcp::TCPStateTest::syn(*link);
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  app.nativeAllocator = &a; app.nativePool = &pool;
  ZmRef<ZiIOBuf> original = new Zi::IOBufStub<ZiIOBuf>{
    a.bytes, sizeof(a.bytes), &a, nullptr, 64, false, true};
  original->skip = 256;
  bool admitted = link->enqueue_(original);
  Zutcp::TxRange range;
  bool selected = link->txRange(0, range);
  auto ref = Zutcp::SentFrameRef::data(range, false);
  unsigned attempts = 0;
  bool geometry = true;
  bool sent = link->output(ref, [&a, &attempts, &geometry, accept, syn](
      auto buf, const Zxdp::CommitFn &commit) {
    ++attempts;
    geometry &= buf->data() == a.bytes + 256 && buf->length == 64 &&
      bool(buf->tcp.flags & 0x02) == syn && buf->retained;
    if (!accept || !commit()) return false;
    buf->retained->adopt(&a, a.bytes, sizeof(a.bytes));
    buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
    return true;
  }, syn);
  ZuCHECK(admitted && selected && attempts == 1 && geometry && sent == accept &&
      link->txRangeCount() == unsigned(!accept) && link->txNext() == (accept ? 64 : 0) &&
      Zutcp::TCPStateTest::records(*link) == unsigned(accept) && !a.returned,
    "raw refusal restores the whole original; accepted TCP carries no inner prefix or copy");
  if (accept) {
    if (syn) Zutcp::TCPStateTest::fallback(*link);
    Zutcp::TCPStateTest::ack(*link, 32);
    ZuCHECK(link->txAckd() == 32 && !a.returned && link->txUnackdBytes() == 32,
      "partial cumulative ACK retains the original including a rejected-SYN fallback");
    Zutcp::TCPStateTest::ack(*link, 64);
    ZuCHECK(link->txAckd() == 64 && !link->txUnackdBytes() && a.returned == 1,
      "full cumulative ACK retires retained original once without any peer packet ACK");
  }
  link->finish(); link = nullptr; ref = {}; range = {}; original = nullptr;
  ZuCHECK(a.valid && a.returned == 1, "all refusal/accepted/fallback owners drain backing once");
}

static void originalTxOrder(unsigned scenario)
{
  ZuTestScopeRT(originalTxOrder);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  app.nativeAllocator = &a; app.nativePool = &pool;
  ZmRef<ZiIOBuf> original = new Zi::IOBufStub<ZiIOBuf>{
    a.bytes, sizeof(a.bytes), &a, nullptr, 64, false, true};
  original->skip = 256;
  bool admitted = link->enqueue_(original);
  Zutcp::TxRange range;
  bool selected = link->txRange(0, range);
  auto ref = Zutcp::SentFrameRef::data(range, false);
  unsigned attempts = 0;
  bool before = true, nested = false;
  bool sent = link->output(ref, [
    link, &a, &ref, &attempts, &before, &nested, scenario](
	auto buf, const Zxdp::CommitFn &commit) {
    ++attempts;
    before &= !Zutcp::TCPStateTest::records(*link) &&
      !Zutcp::TCPStateTest::flight(*link) && !link->txNext() &&
      !link->txUnackdBytes() && !a.returned && buf->retained &&
      buf->data() == a.bytes + 256 && buf->length == 64;
    if (scenario == 3) {
      nested = link->output(ref, [&attempts](
	  auto, const Zxdp::CommitFn &commit) {
	if (!commit()) return false;
	++attempts;
	return true;
      });
    }
    if (scenario == 1) return false;
    if (scenario == 2) { Zutcp::TCPStateTest::reset(*link); return commit(); }
    if (!commit()) return false;
    buf->retained->adopt(&a, a.bytes, sizeof(a.bytes));
    buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
    return true;
  });
  ZuCHECK(admitted && selected && before && !nested && attempts == 1 &&
      sent == (scenario != 1 && scenario != 2) && !a.returned,
    "ordinary DATA reaches the backend with its retained backing before new recovery "
    "accounting; nested DATA cannot publish an uncommitted original twice");
  if (scenario == 1) {
    uint64_t next = 0, ack = 0, sentAck = 0;
    ZuCHECK(Zutcp::TCPStateTest::frontiers(*link, next, ack, sentAck) && !next,
      "refused output does not publish an ACK-admissible frontier");
    ZuCHECK(!Zutcp::TCPStateTest::records(*link) && !Zutcp::TCPStateTest::flight(*link) &&
	!link->txNext() && !link->txUnackdBytes() && link->txRangeCount() == 1 &&
	original->data() == a.bytes + 256 && original->length == 64,
      "refusal creates no recovery debt and preserves the original application view");
    sent = link->output(ref, [&a, &attempts](auto buf, const Zxdp::CommitFn &commit) {
      if (!commit()) return false;
      ++attempts;
      buf->retained->adopt(&a, a.bytes, sizeof(a.bytes));
      buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
      return true;
    });
  }
  if (scenario == 2) {
    uint64_t next = 0, ack = 0, sentAck = 0;
    ZuCHECK(Zutcp::TCPStateTest::frontiers(*link, next, ack, sentAck) && !next,
      "reset before commit leaves the replacement generation frontier empty");
    ZuCHECK(!sent && !Zutcp::TCPStateTest::records(*link) &&
	!Zutcp::TCPStateTest::flight(*link) && !link->txNext() && !link->txUnackdBytes(),
      "stale commit refuses publication without losing the original backing");
  } else {
    ZuCHECK(sent && Zutcp::TCPStateTest::records(*link) == 1 &&
	Zutcp::TCPStateTest::flight(*link) == 64 && link->txNext() == 64 &&
	link->txUnackdBytes() == 64 && !link->txRangeCount(),
      "accepted original commits complete recovery and congestion state on the same Tx turn");
    Zutcp::TCPStateTest::ack(*link, 32);
    ZuCheck(link->txAckd() == 32 && !a.returned && link->txUnackdBytes() == 32);
    Zutcp::TCPStateTest::ack(*link, 64);
    ZuCheck(link->txAckd() == 64 && a.returned == 1 && !link->txUnackdBytes());
  }
  link->finish(); link = nullptr; ref = {}; range = {}; original = nullptr;
  ZuCHECK(a.valid && a.returned == 1,
    "refusal retry, ACK retirement and generation reset each return the physical original once");
}

static void acceptedFrontierBurst()
{
  ZuTestScope(acceptedFrontierBurst);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  app.nativeAllocator = &a; app.nativePool = &pool;
  ZmRef<ZiIOBuf> original = new Zi::IOBufStub<ZiIOBuf>{
    a.bytes, sizeof(a.bytes), &a, nullptr, 64, false, true};
  original->skip = 256;
  bool admitted = link->enqueue_(original);
  Zutcp::TxRange range;
  bool selected = link->txRange(0, range);
  auto ref = Zutcp::SentFrameRef::data(range, false);
  uint8_t expected[32];
  for (unsigned i = 0; i < sizeof(expected); ++i) expected[i] = uint8_t(i + 1);
  app.holdTx = app.holdRx = true;
  bool unpublished = false, beforeRecovery = false, received = true, immediate = false;
  bool sent = link->output(ref, [link, &app, &a, &expected, &unpublished,
      &beforeRecovery, &received, &immediate](auto buf, const Zxdp::CommitFn &commit) {
    uint64_t next = 0, ack = 0, sentAck = 0;
    unpublished = Zutcp::TCPStateTest::frontiers(*link, next, ack, sentAck) && !next;
    if (!commit()) return false;
    buf->retained->adopt(&a, a.bytes, sizeof(a.bytes));
    buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
    // Model immediate peer traffic after descriptor publication, before return.
    beforeRecovery = Zutcp::TCPStateTest::frontiers(*link, next, ack, sentAck) &&
      next == 64 && !ack && !link->txNext() &&
      !Zutcp::TCPStateTest::records(*link) && !a.returned;
    for (unsigned i = 0; i < sizeof(expected); ++i)
      received &= link->receive(input(app, {expected + i, 1}, i, 64));
    uint8_t bytes[sizeof(expected)];
    unsigned n = link->reader().copy(0, {bytes, sizeof(bytes)});
    immediate = n == sizeof(bytes) && !memcmp(bytes, expected, sizeof(bytes)) &&
      !app.rxJobs.count_() && !Zutcp::TCPStateTest::records(*link) && !a.returned;
    return true;
  });
  ZuCHECK(admitted && selected && sent && unpublished && beforeRecovery && received && immediate,
    "pre-publication commit admits an exact 32-packet burst before the backend returns");
  ZuCHECK(Zutcp::TCPStateTest::records(*link) == 1 && link->txNext() == 64 &&
      !link->txAckd() && !a.returned,
    "queued peer ACKs cannot retire the original before its recovery bookkeeping commits");
  app.holdTx = false; app.runTx();
  app.holdRx = false; app.runRx(); app.runTx();
  unsigned consumed = 0;
  while (unsigned n = consume(*link)) consumed += n;
  ZuCHECK(link->txAckd() == 64 && !Zutcp::TCPStateTest::records(*link) &&
      a.returned == 1 && consumed == sizeof(expected),
    "normal owner dispatch retires the accepted backing once and preserves the entire burst");
  link->finish(); link = nullptr; ref = {}; range = {}; original = nullptr;
  ZuCheck(a.valid && a.returned == 1);
}

static void committedACKTimestamp()
{
  ZuTestScope(committedACKTimestamp);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::timestamps(*link, 100);
  Zutcp::TCPStateTest::advanceRx(*link, 5);
  bool unchanged = false;
  bool refused = !link->output({}, [link, &unchanged](auto buf, const Zxdp::CommitFn &) {
    uint64_t next = 0, ack = 0, sent = 0;
    unchanged = buf->tcp.ack == 2006 &&
      Zutcp::TCPStateTest::frontiers(*link, next, ack, sent) && !sent &&
      !Zutcp::TCPStateTest::learnTimestamp(*link, 5, 101) &&
      Zutcp::TCPStateTest::recent(*link) == 100;
    return false;
  });
  bool exact = false, learned = false, bounded = false;
  bool accepted = link->output({}, [link, &exact, &learned, &bounded](
      auto buf, const Zxdp::CommitFn &commit) {
    // A later Rx publication/header read must not alter this packet's evidence.
    Zutcp::TCPStateTest::advanceRx(*link, 9);
    auto later = Zutcp::TCPStateTest::header(*link);
    if (!commit()) return false;
    uint64_t next = 0, ack = 0, sent = 0;
    exact = buf->tcp.ack == 2006 && later.ack == 2010 &&
      Zutcp::TCPStateTest::frontiers(*link, next, ack, sent) && sent == 5;
    learned = Zutcp::TCPStateTest::learnTimestamp(*link, 5, 101) &&
      Zutcp::TCPStateTest::recent(*link) == 101;
    bounded = !Zutcp::TCPStateTest::learnTimestamp(*link, 6, 102) &&
      Zutcp::TCPStateTest::recent(*link) == 101 &&
      Zutcp::TCPStateTest::learnTimestamp(*link, 4, 103) &&
      Zutcp::TCPStateTest::recent(*link) == 103;
    return true;
  });
  ZuCHECK(refused && unchanged && accepted && exact && learned && bounded,
    "only the committed packet's exact ACK advances Last.ACK.sent before backend return; "
    "TS.Recent accepts covered sequence and excludes later sequence");
  link->finish();
}

static void probeQuota(bool accept)
{
  ZuTestScopeRT(probeQuota);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  auto ref = retained(a, pool);
  bool setup = Zutcp::TCPStateTest::record(*link, ref);
  bool first = false, reentry = true, second = true;
  unsigned attempts = 0;
  Zutcp::TCPStateTest::probe(*link, [
    link, &ref, &first, &reentry, &second, &attempts, accept] {
    first = link->output(ref, [link, &ref, &attempts, &reentry, accept](
	auto buf, const Zxdp::CommitFn &commit) {
      ++attempts;
      reentry = link->output(ref, [](auto, const Zxdp::CommitFn &commit) { return commit(); });
      if (!accept || !commit()) return false;
      buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
      return true;
    });
    if (accept) second = link->output(ref, [](
	auto, const Zxdp::CommitFn &commit) { return commit(); });
  });
  ZuCHECK(setup && first == accept && !reentry && (!accept || !second) && attempts == 1 &&
      Zutcp::TCPStateTest::latest(*link, ref) == unsigned(accept) && !a.returned,
    "one accepted TLP probe consumes quota across raw-send reentry and preserves chronology");
  link->finish(); link = nullptr; ref = {};
  ZuCHECK(a.valid && a.returned == 1, "TLP refusal and accepted histories release one original");
}

static void admissionAndACK()
{
  ZuTestScope(admissionAndACK);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  auto ref = retained(a, pool, 4);
  bool setup = Zutcp::TCPStateTest::record(*link, ref);
  app.allowRx = false;
  bool refused = link->receive(input(app, "tail", 4, 4));
  ZuCHECK(setup && refused && link->txAckd() == 4 && !link->rxPending() &&
      !link->rxAdmitted() && !link->rxBytes() && !link->disconnecting() && !link->resets,
    "real TCP ACK retires outgoing bytes while refused out-of-order payload commits no error");
  app.allowRx = true;
  bool retried = link->receive(input(app, "tail", 4, 4));
  ZuCHECK(retried && link->rxPending() == 1 && link->rxAdmitted() == 4 &&
      !link->reader().length() && !link->rxBytes(),
    "same sequence range remains admissible after Rx capacity returns; no packet-number gate");
  link->finish(); link = nullptr; ref = {};
  ZuCHECK(a.valid && a.returned == 1, "independent ACK retirement retains no extra owner");
}

static void invalidACKInput(bool stale)
{
  ZuTestScopeRT(invalidACKInput);
  App app;
  ZmRef<Link> link = new Link{&app};
  app.holdTx = app.holdRx = true;
  bool first = link->receive(input(app, "bad", 0, 1));
  bool duplicate = link->receive(input(app, "bad", 0, 1));
  ZuCHECK(first && duplicate && !app.demands && !app.rxJobs.count_() &&
      !link->processed && !link->rxAdmitted(),
    "future ACKs are rejected directly without retaining packets or scheduling replay");
  if (stale) Zutcp::TCPStateTest::reset(*link);
  app.holdTx = false; app.runTx(); app.holdRx = false; app.runRx();
  ZuCHECK(!link->processed && !link->rxAdmitted() && !link->disconnecting(),
    "future ACK and replaced-generation input cannot deliver payload or corrupt the live owner");
  link->finish();
}

static void directACKAdmission(unsigned scenario)
{
  ZuTestScopeRT(directACKAdmission);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  app.holdTx = app.holdRx = true;
  auto ref = retained(a, pool, 4);
  bool received = true;
  unsigned attempts = scenario == 4 ? 32 : 1;
  for (unsigned i = 0; i < attempts; ++i)
    received &= link->receive(input(app, scenario ? ZuBSpan{"bad"} : ZuBSpan{}, 0, 4));
  ZuCHECK(received && !app.demands && !link->rxAdmitted() && !app.rxJobs.count_(),
    "uncommitted ACK targets retain no ingress, even during a burst");
  bool setup = Zutcp::TCPStateTest::record(*link, ref);
  if (scenario == 3) {
    bool reset = link->receive(input(app, {}, 0, 4, 0x14));
    ZuCHECK(setup && reset && link->abortingRx() && !link->rxAdmitted(),
      "sequence-qualified peer RST needs no pending-validation drain");
  } else if (scenario == 1) {
    bool tail = link->receive(input(app, "tail", 4, 4));
    bool head = link->receive(input(app, "head", 0, 4));
    // Gap closure schedules the normal Rx dequeue; Tx ACK work remains held.
    app.holdRx = false; app.runRx();
    uint8_t bytes[8];
    auto n = link->reader().copy(0, {bytes, sizeof(bytes)});
    ZuCHECK(setup && tail && head && n == 8 && !memcmp(bytes, "headtail", 8),
      "ordinary reassembly, not an ACK-validation queue, orders valid gap-filling data");
  } else {
    unsigned n = scenario == 2 ? 32 : 1;
    for (unsigned i = 0; i < n; ++i)
      received &= link->receive(input(app, "x", i, 4));
    ZuCHECK(setup && received && link->reader().length() == n && app.demands == n &&
	!app.rxJobs.count_(),
      "committed ACK targets admit every contiguous packet immediately with Tx work held");
  }
  app.holdTx = false; app.runTx(); app.holdRx = false; app.runRx(); app.runTx();
  if (scenario == 3)
    ZuCheck(link->disconnecting() && !link->processed && !link->resets);
  else
    ZuCheck(link->txAckd() == 4 && a.returned == 1);
  link->finish(); link = nullptr; ref = {};
  ZuCheck(a.valid && a.returned == 1);
}

static void ackHoldTimer(bool disabled)
{
  ZuTestScopeRT(ackHoldTimer);
  App app{disabled ? ZuTime{0} : ZuTime{0, 50000}};
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::requestACK(*link, true);
  auto deadline = Zutcp::TCPStateTest::ackDeadline(*link);
  ZuCHECK(Zutcp::TCPStateTest::pendingACK(*link) &&
      (disabled ? !Zutcp::TCPStateTest::heldACK(*link) && !*deadline :
	Zutcp::TCPStateTest::heldACK(*link) &&
	deadline == app.clock + ZuTime{0, 50000}),
    "configured zero sends immediately; positive hold arms one ACK deadline");
  if (!disabled) {
    app.clock += ZuTime{0, 10000};
    Zutcp::TCPStateTest::requestACK(*link, true);
    ZuCHECK(Zutcp::TCPStateTest::ackDeadline(*link) == deadline,
      "newer cumulative ACK keeps the first deadline");
    Zutcp::TCPStateTest::requestACK(*link, false);
    ZuCHECK(!Zutcp::TCPStateTest::heldACK(*link) &&
	Zutcp::TCPStateTest::ackDeadline(*link) == deadline &&
	Zutcp::TCPStateTest::pendingACK(*link),
      "urgent ACK overrides a held ACK");
    bool sent = link->output({}, [](auto, const Zxdp::CommitFn &commit) { return commit(); });
    ZuCHECK(sent && !Zutcp::TCPStateTest::pendingACK(*link) &&
	Zutcp::TCPStateTest::ackDeadline(*link) == deadline,
      "accepted ACK clears pending state while retaining the timer epoch");
    Zutcp::TCPStateTest::requestACK(*link, true);
    ZuCHECK(Zutcp::TCPStateTest::heldACK(*link) &&
	Zutcp::TCPStateTest::ackDeadline(*link) == deadline,
      "next cumulative ACK reuses the earlier timer epoch");
    link->output({}, [](auto, const Zxdp::CommitFn &commit) { return commit(); });
    app.clock = deadline;
    Zutcp::TCPStateTest::expireACK(*link);
    ZuCHECK(!Zutcp::TCPStateTest::pendingACK(*link) &&
	!*Zutcp::TCPStateTest::ackDeadline(*link),
      "unused ACK deadline expires without sending a redundant ACK");
    Zutcp::TCPStateTest::requestACK(*link, true);
    deadline = Zutcp::TCPStateTest::ackDeadline(*link);
    app.clock = deadline;
    Zutcp::TCPStateTest::expireACK(*link);
    ZuCHECK(!Zutcp::TCPStateTest::heldACK(*link) &&
	!*Zutcp::TCPStateTest::ackDeadline(*link) &&
	Zutcp::TCPStateTest::pendingACK(*link),
      "deadline releases one held ACK for standalone transmission");
  }
  link->finish();
}

static void ackHoldCoalesce()
{
  ZuTestScopeRT(ackHoldCoalesce);
  App app;
  ZmRef<Link> link = new Link{&app};
  app.holdTx = app.holdRx = true;
  Zutcp::TCPStateTest::requestACK(*link, false);
  Zutcp::TCPStateTest::advanceRx(*link, 5);
  Zutcp::TCPStateTest::requestACK(*link, true);
  ZuCHECK(app.txJobs.count_() == 1 && Zutcp::TCPStateTest::postedACK(*link),
    "held updates coalesce behind one pending urgent owner request");
  app.holdTx = false; app.runTx();
  Zutcp::TCPHeader header;
  bool sent = link->output({}, [&header](
      auto buf, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    header = buf->tcp;
    return true;
  });
  ZuCHECK(sent && header.ack == 2006 && !Zutcp::TCPStateTest::postedACK(*link) &&
      !app.rxJobs.count_(),
    "one Tx request reads the latest cumulative ACK without an Rx completion or reissue");
  app.holdTx = true;
  Zutcp::TCPStateTest::requestACK(*link, true);
  auto old = app.txJobs.shift();
  Zutcp::TCPStateTest::reset(*link);
  app.holdTx = false; app.runTx();
  app.holdTx = true;
  Zutcp::TCPStateTest::requestACK(*link, true);
  if (old) old();
  ZuCHECK(Zutcp::TCPStateTest::postedACK(*link),
    "an old generation request cannot clear the new generation's pending ACK tag");
  app.holdTx = false; app.runTx();
  ZuCheck(!Zutcp::TCPStateTest::postedACK(*link));
  link->finish();
}

static void ackHoldByteBound()
{
  ZuTestScopeRT(ackHoldByteBound);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::rxMSS(*link, 100);
  Zutcp::TCPStateTest::advanceRx(*link, 200);
  Zutcp::TCPStateTest::requestACK(*link, true);
  ZuCHECK(Zutcp::TCPStateTest::pendingACK(*link) &&
      !Zutcp::TCPStateTest::heldACK(*link),
    "two advertised MSS of new data releases the latest cumulative ACK");
  link->finish();
}

// Exercise committed SYN publication and its one-way handshake owner event.
// A setup retransmission includes an accepted duplicate-triggered SYNACK, not
// only timeout retries; either kind must make its RTT ambiguous under Karn.
static void handshakeRTT(bool server, unsigned scenario)
{
  ZuTestScopeRT(handshakeRTT);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::handshake(*link, server);
  unsigned attempts = 0;
  Zutcp::TCPHeader wire;
  auto send = [&attempts, &wire](auto buf, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    ++attempts; wire = buf->tcp; return true;
  };
  bool firstRefused = true;
  if (scenario == 1) {
    firstRefused = !link->output({}, [&attempts](
	auto, const Zxdp::CommitFn &) { ++attempts; return false; }, true);
    app.clock += Zutcp::timeUS(30000);
  }
  bool accepted = link->output({}, send, true);
  bool header = wire.seq == 1000 && wire.ack == unsigned(server ? 2001 : 0) &&
    (wire.flags & 0x12) == unsigned(server ? 0x12 : 0x02);
  if (scenario == 2) {
    app.clock += Zutcp::timeUS(10000);
    accepted &= link->output({}, send, true);
  } else if (scenario == 3) {
    Zutcp::TCPStateTest::synTimeout(*link);
    accepted &= link->synCalls == 1;
    accepted &= link->output({}, send, true);
  }
  app.clock += Zutcp::timeUS(20000);
  bool invalid = true;
  if (scenario == 4) {
    auto d = input(app, {}, 0, 1, server ? 0x10 : 0x12);
    if (!server) d.tcp.seq = 2000;
    invalid = (server ? link->receive(ZuMv(d)) :
      !Zutcp::TCPStateTest::handshakeACK(*link, 1)) &&
      !Zutcp::TCPStateTest::confirmed(*link) &&
      !*Zutcp::TCPStateTest::handshakeRTT(*link);
  }
  app.holdTx = app.holdRx = scenario == 5;
  auto d = input(app, {}, 0, 0, server ? 0x10 : 0x12);
  if (!server) d.tcp.seq = 2000;
  bool received = server ? link->receive(ZuMv(d)) :
    Zutcp::TCPStateTest::handshakeACK(*link, 0);
  bool delayed = scenario != 5 ||
    (Zutcp::TCPStateTest::rxConfirmed(*link) &&
      !Zutcp::TCPStateTest::confirmed(*link) && !app.rxJobs.count_());
  if (scenario == 5) Zutcp::TCPStateTest::reset(*link);
  app.holdTx = app.holdRx = false; app.runTx(); app.runRx(); app.runTx();
  auto sample = Zutcp::TCPStateTest::handshakeRTT(*link);
  bool measured = scenario != 2 && scenario != 3 && scenario != 5;
  ZuCHECK(firstRefused && accepted && header && invalid && received && delayed &&
      (*sample == measured) && (!measured || sample == Zutcp::timeUS(20000)) &&
      (Zutcp::TCPStateTest::confirmed(*link) == (scenario != 5)),
    "only a valid current ACK of one accepted SYN/SYNACK produces one exact RTT sample");
  link->finish();
}

static void synCommitAdmission(unsigned scenario)
{
  ZuTestScopeRT(synCommitAdmission);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::handshake(*link, false);
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  app.nativeAllocator = &a; app.nativePool = &pool;
  Zutcp::SentFrameRef ref;
  if (scenario) ref = retained(a, pool);
  unsigned end = scenario ? 64 : 0, ack = scenario == 1 ? 64 : 0;
  bool unsent = !Zutcp::TCPStateTest::handshakeACK(*link, ack) &&
    !Zutcp::TCPStateTest::handshakeACK(*link, ack, true);
  app.holdTx = app.holdRx = true;
  bool qualified = false, exposed = false, resetBound = false;
  bool sent = link->output(ref, [link, &exposed, &qualified, &resetBound, end, ack](
      auto buf, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    exposed = Zutcp::TCPStateTest::synEnd(*link) == end;
    resetBound = !Zutcp::TCPStateTest::handshakeACK(*link, end + 1, true) &&
      Zutcp::TCPStateTest::handshakeACK(*link, end, true);
    qualified = Zutcp::TCPStateTest::handshakeACK(*link, ack);
    if (end) { buf->data__ = 0; buf->size = buf->skip = buf->length = 0; }
    return true;
  }, true);
  ZuCHECK(unsent && sent && exposed && resetBound && qualified &&
      Zutcp::TCPStateTest::rxConfirmed(*link) &&
      !Zutcp::TCPStateTest::confirmed(*link) && !app.rxJobs.count_(),
    "committed empty/TFO SYN qualifies the peer immediately; unsent or excess RST ACK does not");
  if (scenario == 2) {
    bool fallback = link->output({}, [](auto, const Zxdp::CommitFn &commit) {
      if (!commit()) return false;
      return true;
    }, true);
    ZuCHECK(fallback && Zutcp::TCPStateTest::synEnd(*link) == 64,
      "an empty fallback SYN preserves the original committed Fast Open extent");
  }
  app.holdTx = false; app.runTx();
  ZuCHECK(Zutcp::TCPStateTest::confirmed(*link) && !app.rxJobs.count_(),
    "one-way handshake bookkeeping requires no retained packet or Rx completion");
  link->finish(); link = nullptr; ref = {};
  ZuCheck(a.valid && a.returned == unsigned(bool(scenario)));
}

static void selectiveRecovery()
{
  ZuTestScope(selectiveRecovery);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}, b{app.mx()}, c{app.mx()}, d{app.mx()};
  Zxdp::BufPool pool{4, sizeof(a.bytes)};
  auto first = retained(a, pool, 64, 0), second = retained(b, pool, 64, 64);
  auto third = retained(c, pool, 64, 128), fourth = retained(d, pool, 64, 192);
  bool setup = Zutcp::TCPStateTest::record(*link, first) &&
    Zutcp::TCPStateTest::record(*link, second) &&
    Zutcp::TCPStateTest::record(*link, third) && Zutcp::TCPStateTest::record(*link, fourth);
  Zutcp::TCPOptions sack;
  sack.sackCount = 1; sack.sack[0] = {64, 256};
  Zutcp::TCPStateTest::ack(*link, 0, sack);
  ZuCHECK(setup && Zutcp::TCPStateTest::discounted(*link) == 192 &&
      !a.returned && !b.returned && !c.returned && !d.returned && !link->txAckd(),
    "selective hints discount flight but cannot release any original or advance cumulative ACK");
  Zutcp::TCPStateTest::ack(*link, 64, sack);
  sack.sackCount = 2; sack.sack[0] = {0, 64}; sack.sack[1] = {128, 256};
  Zutcp::TCPStateTest::ack(*link, 64, sack);
  ZuCHECK(a.returned == 1 && !b.returned && !c.returned && !d.returned &&
      Zutcp::TCPStateTest::dsacks(*link) == 1,
    "cumulative ACK alone releases its canonical lease; DSACK reports duplicate delivery");
  Zutcp::TCPStateTest::expireRTO(*link);
  Zutcp::SentFrameRef retry;
  bool selected = link->next(retry);
  ZuCHECK(selected && retry.offset == 64 && retry.length == 64 &&
      retry.range.storage.ptr() == second.range.storage.ptr() &&
      retry.range.storage->data + retry.range.offset == b.bytes + 256 &&
      !Zutcp::TCPStateTest::discounted(*link) && !b.returned,
    "RTO forgets renegable SACK hints and recovers the whole oldest retained original");
  Zutcp::TCPStateTest::ack(*link, 256);
  ZuCHECK(a.returned == 1 && b.returned == 1 && c.returned == 1 && d.returned == 1 &&
      !link->txUnackdBytes(), "cumulative recovery releases each hinted original once");
  link->finish(); link = nullptr;
  first = second = third = fourth = retry = {};
  ZuCHECK(a.valid && b.valid && c.valid && d.valid, "recovery preserves canonical allocators");
}

static void cumulativeFallback()
{
  ZuTestScope(cumulativeFallback);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::disableSACK(*link);
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  auto ref = retained(a, pool);
  bool setup = Zutcp::TCPStateTest::record(*link, ref);
  Zutcp::TCPStateTest::ack(*link, 0);
  Zutcp::TCPStateTest::ack(*link, 0);
  Zutcp::SentFrameRef retry;
  bool early = link->next(retry);
  Zutcp::TCPStateTest::ack(*link, 0);
  bool selected = link->next(retry);
  ZuCHECK(setup && !early && selected && retry.range.storage.ptr() == ref.range.storage.ptr() &&
      retry.offset == 0 && retry.length == 64 && !a.returned,
    "three valid pure duplicate cumulative ACKs recover one whole original without SACK");
  Zutcp::TCPStateTest::ack(*link, 64);
  link->finish(); link = nullptr; retry = ref = {};
  ZuCHECK(a.valid && a.returned == 1, "fallback recovery releases original once");
}

static void receiveSACK()
{
  ZuTestScope(receiveSACK);
  App app;
  ZmRef<Link> link = new Link{&app};
  bool received = link->receive(input(app, "efgh", 4)) &&
    link->receive(input(app, "ijkl", 12));
  auto holes = Zutcp::TCPStateTest::rxOptions(*link);
  bool duplicate = link->receive(input(app, "efgh", 4));
  auto dsack = Zutcp::TCPStateTest::rxOptions(*link);
  ZuCHECK(received && duplicate && link->rxAdmitted() == 8 && !link->rxBytes() &&
      holes.sackCount == 2 && dsack.sackCount >= 2 && dsack.sack[0].left == 4 &&
      dsack.sack[0].right == 8 && dsack.dsack(0),
    "real sequence holes advertise two SACK blocks and duplicate-first DSACK without "
    "readmission");
  link->finish();
}

static void emptySACK()
{
  ZuTestScope(emptySACK);
  App app;
  Zutcp::DataRxPQueue queue{0};
  Zutcp::RxSACK state;
  auto add = [&app, &queue](uint64_t offset) {
    auto buf = packet(app, "abc");
    queue.add(new Zutcp::DataRxPQueue::Node{
      buf, buf->data(), 3, app.dataRxAllocator(), nullptr, offset});
  };
  add(30);
  state.accepted(30);
  Zutcp::TCPOptions options{.flags = Zutcp::TCPOptions::TS, .tsval = 17, .tsecr = 0};
  bool initial = !state.fill(queue, 0, options) && options.sackCount == 1 &&
    options.sack[0].left == 30;
  queue.clean();
  bool empty = !state.fill(queue, 33, options) && !options.sackCount;
  for (auto &block : options.sack) empty &= !block.left && !block.right;
  ZuCHECK(initial && empty && options.flags == Zutcp::TCPOptions::TS &&
      options.tsval == 17 && !options.tsecr,
    "empty SACK snapshot clears old wire blocks while preserving TS presence and zero echo");
  add(10); add(30);
  bool filled = !state.fill(queue, 0, options);
  ZuCHECK(filled && options.sackCount == 2 && options.sack[0].left == 10 &&
      options.sack[0].right == 13 && options.sack[1].left == 30,
    "empty snapshot resets obsolete report priorities before later holes are selected");
}

static void emptyDSACK()
{
  ZuTestScope(emptyDSACK);
  Zutcp::DataRxPQueue queue{0};
  Zutcp::RxSACK state;
  Zutcp::TCPOptions options{.flags = Zutcp::TCPOptions::TS, .tsval = 0, .tsecr = 31};
  bool recorded = state.duplicate(2, 5, 7);
  bool first = state.fill(queue, 10, options) && options.sackCount == 1 &&
    options.sack[0].left == 2 && options.sack[0].right == 5 && options.dsack(10);
  state.sent(6, true);
  bool old = state.fill(queue, 10, options);
  state.sent(7, false);
  bool unsent = state.fill(queue, 10, options);
  state.sent(7, true);
  bool cleared = !state.fill(queue, 10, options) && !options.sackCount;
  for (auto &block : options.sack) cleared &= !block.left && !block.right;
  ZuCHECK(recorded && first && old && unsent && cleared &&
      options.flags == Zutcp::TCPOptions::TS && !options.tsval && options.tsecr == 31,
    "empty reassembly still reports pending DSACK until the matching actual report completes");
}

static void paws(bool wrap)
{
  ZuTestScopeRT(paws);
  App app;
  ZmRef<Link> link = new Link{&app};
  uint32_t recent = wrap ? uint32_t(-8) : 100;
  Zutcp::TCPStateTest::timestamps(*link, recent);
  auto old = input(app, "old");
  old.tcp.options = {.flags = Zutcp::TCPOptions::TS, .tsval = recent - 1};
  auto missing = input(app, "bad");
  bool refused = link->receive(ZuMv(old)) && link->receive(ZuMv(missing));
  ZuCHECK(refused && !link->rxAdmitted() && !link->rxBytes() && !link->disconnecting() &&
      Zutcp::TCPStateTest::recent(*link) == recent,
    "old or missing negotiated timestamp cannot deliver DATA or corrupt recent evidence");
  uint32_t fresh = wrap ? 7 : 101;
  auto next = input(app, "new");
  next.tcp.options = {.flags = Zutcp::TCPOptions::TS, .tsval = fresh};
  bool accepted = link->receive(ZuMv(next));
  ZuCHECK(accepted && link->rxAdmitted() == 3 && consume(*link) == 3 && link->retireRx(3) &&
      Zutcp::TCPStateTest::recent(*link) == fresh,
    "new timestamp, including wrap, admits exact bytes and refreshes echo from current sequence");
  link->finish();
}

// Each boundary starts with independent timestamp/sequence history.
static void pawsBoundary(unsigned scenario)
{
  ZuTestScopeRT(pawsBoundary);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::timestamps(*link, 100);
  auto since = app.clock;
  uint32_t value = 99;
  if (scenario < 2 || scenario == 5) {
    app.clock += ZuTime{24*24*60*60};
    if (scenario != 0) app.clock += ZuTime{0, 1};
  } else if (scenario == 2) app.clock -= ZuTime{1};
  else value = scenario == 3 ? uint32_t(100) + uint32_t(0x80000000) : 101;
  auto d = input(app, "new", scenario == 4 ? 4096 : 0);
  if (scenario != 5) d.tcp.options = {.flags = Zutcp::TCPOptions::TS, .tsval = value};
  app.holdTx = app.holdRx = true;
  bool qualified = link->acceptsLiveTCP(d);
  bool received = link->receive(ZuMv(d));
  bool accepted = scenario == 1;
  ZuCHECK(received && qualified == (accepted || scenario == 5) && !link->disconnecting() &&
      link->rxAdmitted() == unsigned(accepted ? 3 : 0) &&
      link->rxBytes() == unsigned(accepted ? 3 : 0) &&
      Zutcp::TCPStateTest::recent(*link) == unsigned(accepted ? value : 100) &&
      Zutcp::TCPStateTest::recentTime(*link) == (accepted ? app.clock : since),
    "PAWS is valid at exactly 24 days and across backward time; expiry is strict, "
    "half-range is old, unacceptable sequence never refreshes TS evidence, and expired "
    "missing-TS input can qualify a live tuple but cannot admit DATA");
  if (accepted)
    ZuCheck(consume(*link) == 3 && link->retireRx(3));
  link->finish();
}

static void pawsClock(unsigned scenario)
{
  ZuTestScopeRT(pawsClock);
  App app;
  ZmRef<Link> link = new Link{&app};
  if (scenario != 2)
    Zutcp::TCPStateTest::timestamps(*link, scenario == 3 ? uint32_t(-1) : 100);
  auto d = input(app, "new");
  bool timestamp = !scenario || scenario == 3;
  if (timestamp)
    d.tcp.options = {.flags = Zutcp::TCPOptions::TS, .tsval = scenario == 3 ? 0U : 101U};
  app.holdTx = app.holdRx = true;
  app.clockCalls = 0;
  bool qualified = link->acceptsLiveTCP(d);
  auto routeClocks = app.clockCalls;
  app.clockCalls = 0;
  bool received = link->receive(ZuMv(d));
  auto receiveClocks = app.clockCalls;
  bool accepted = scenario != 1;
  ZuCHECK(qualified == accepted && received && routeClocks == unsigned(scenario == 1) &&
      receiveClocks == unsigned(timestamp) &&
      link->rxAdmitted() == unsigned(accepted ? 3 : 0) &&
      link->rxBytes() == unsigned(accepted ? 3 : 0) &&
      (scenario != 3 || !Zutcp::TCPStateTest::recent(*link)),
    "fresh TS, including wrapped zero, needs no routing clock; missing TS checks cache "
    "expiry; admission samples once only for present negotiated TS with owner jobs held");
  if (accepted)
    ZuCheck(consume(*link) == 3 && link->retireRx(3));
  link->finish();
}

static void pawsAdmission(unsigned scenario)
{
  ZuTestScopeRT(pawsAdmission);
  bool stale = scenario == 1;
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::timestamps(*link, 100);
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  app.holdTx = app.holdRx = true;
  auto ref = retained(a, pool, 4);
  auto d = input(app, "tail", 0, 4);
  d.tcp.options = {.flags = Zutcp::TCPOptions::TS, .tsval = 101};
  bool received = link->receive(ZuMv(d));
  bool setup = Zutcp::TCPStateTest::record(*link, ref);
  ZuCHECK(setup && received && !app.demands && !app.rxJobs.count_() &&
      !link->rxAdmitted() && !link->txAckd() &&
      Zutcp::TCPStateTest::recent(*link) == 100,
    "uncommitted ACK rejects DATA without updating recent TS or retaining ingress");
  // A fresh retransmission must use current PAWS evidence and arrival time.
  app.clock += ZuTime{1};
  if (scenario) Zutcp::TCPStateTest::timestamps(*link, 102);
  if (scenario == 2) app.clock += ZuTime{24*24*60*60} + ZuTime{0, 1};
  d = input(app, "tail", 0, 4);
  d.tcp.options = {.flags = Zutcp::TCPOptions::TS, .tsval = 101};
  received &= link->receive(ZuMv(d));
  app.holdTx = false; app.runTx();
  app.holdRx = false; app.runRx(); app.runTx(); app.runRx();
  ZuCHECK(received && !link->disconnecting() &&
      link->rxAdmitted() == unsigned(stale ? 0 : 4) &&
      link->rxBytes() == unsigned(stale ? 0 : 4) &&
      link->txAckd() == unsigned(stale ? 0 : 4) &&
      a.returned == unsigned(!stale) &&
      Zutcp::TCPStateTest::recent(*link) == unsigned(stale ? 102 : 101) &&
      Zutcp::TCPStateTest::recentTime(*link) == app.clock,
    "new input validates PAWS at its arrival: fresh or expired-cache DATA and advancing "
    "ACK commit together; live stale evidence cannot advance either frontier");
  if (!stale)
    ZuCheck(consume(*link) == 4 && link->retireRx(4));
  link->finish(); link = nullptr; ref = {};
  ZuCHECK(a.valid && a.returned == 1,
    "accepted ACK or final teardown returns the retained outgoing backing exactly once");
}

static void mssBudget(bool v6, unsigned peer, bool sack)
{
  ZuTestScopeRT(mssBudget);
  App app;
  ZmRef<Link> link = new Link{&app};
  unsigned expected = peer ? peer : unsigned(v6 ? 1220 : 536);
  if (expected > 1200) expected = 1200;
  if (sack) expected -= 36;
  unsigned published = Zutcp::TCPStateTest::mss(*link, peer, sack, v6);
  ZuCHECK(published == expected && published > 0,
    "standard TCP application budget charges only negotiated option bytes against MSS and path");
  if (peer == 800) {
    bool fits = Zutcp::TCPStateTest::cap(*link, 1000);
    bool invalid = !Zutcp::TCPStateTest::cap(*link, 700);
    ZuCHECK(fits && invalid && !link->dataMPS() && link->txAdmissionClosed() &&
        Zutcp::TCPStateTest::failedBudget(*link),
      "cap reduction above the published promise survives; invalidating it closes admission");
  }
  link->finish();
}

static void positiveWindow(unsigned window)
{
  ZuTestScopeRT(positiveWindow);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  auto ref = retained(a, pool);
  Zutcp::TCPStateTest::window(*link, window);
  unsigned attempts = 0;
  bool sent = link->output(ref, [&attempts](auto buf, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    ++attempts;
    buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
    return true;
  });
  ZuCHECK(sent == bool(window) && attempts == unsigned(bool(window)) &&
      link->txNext() == unsigned(window ? 64 : 0) && !a.returned,
    "zero window blocks output; positive TCP window admits one canonical whole original");
  if (window) {
    Zutcp::TCPStateTest::window(*link, 0);
    unsigned before = attempts;
    bool blocked = !link->output(ref, [&attempts](
	auto, const Zxdp::CommitFn &commit) {
      if (!commit()) return false;
      ++attempts;
      return true;
    });
    ZuCHECK(blocked && attempts == before && Zutcp::TCPStateTest::persisting(*link),
      "zero-window outstanding data selects persist while suppressing DATA RTO and TLP");
    Zutcp::TCPStateTest::ack(*link, 64);
  }
  link->finish(); link = nullptr; ref = {};
  ZuCHECK(a.valid && a.returned == 1, "window changes preserve canonical original ownership");
}

static void pacedECN()
{
  ZuTestScope(pacedECN);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}, b{app.mx()}; Zxdp::BufPool pool{2, sizeof(a.bytes)};
  auto first = retained(a, pool), second = retained(b, pool, 64, 64);
  Zutcp::TCPStateTest::pacingRTT(*link);
  Zutcp::TCPStateTest::ecn(*link);
  unsigned attempts = 0;
  auto accept = [&attempts](auto buf, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    ++attempts;
    buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
    return true;
  };
  bool sent = link->output(first, accept);
  uint64_t deadline = Zutcp::TCPStateTest::paced(*link);
  bool blocked = !link->output(second, accept);
  bool ack = link->output({}, [&attempts](
      auto, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    ++attempts;
    return true;
  });
  uint64_t before = Zutcp::TCPStateTest::cwnd(*link);
  Zutcp::TCPStateTest::ece(*link);
  uint64_t reduced = Zutcp::TCPStateTest::cwnd(*link);
  Zutcp::TCPStateTest::ece(*link);
  ZuCHECK(sent && blocked && ack && attempts == 2 && deadline > uint64_t(app.clock.microsecs()) &&
      reduced < before && Zutcp::TCPStateTest::cwnd(*link) == reduced &&
      Zutcp::TCPStateTest::cwr(*link),
    "accepted DATA charges pacing; pure ACK bypasses it; same-flight ECE reduces once and "
    "queues CWR");
  Zutcp::TCPStateTest::pacingDue(*link);
  bool refused = !link->output(second, [](auto, const Zxdp::CommitFn &) { return false; });
  bool retained = Zutcp::TCPStateTest::cwr(*link);
  Zutcp::TCPHeader header;
  bool accepted = link->output(second, [&attempts, &header](
      auto buf, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    ++attempts; header = buf->tcp;
    buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
    return true;
  });
  ZuCHECK(refused && retained && accepted && (header.flags & 0x80) &&
      !Zutcp::TCPStateTest::cwr(*link),
    "raw refusal retains CWR and pacing permission; accepted new whole DATA consumes them");
  Zutcp::TCPStateTest::ack(*link, 128);
  link->finish(); link = nullptr; first = second = {};
  ZuCHECK(a.valid && b.valid && a.returned == 1 && b.returned == 1,
    "ECN and pacing preserve each canonical lease through cumulative retirement");
}

static void peerAckMerge(unsigned scenario)
{
  ZuTestScopeRT(peerAckMerge);
  App app{ZuTime{0}, {}, false};
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::stage(*link, false);
  Native source{app.mx()}, replacement{app.mx()};
  Zxdp::BufPool pool{2, sizeof(source.bytes)};
  auto ref = retained(source, pool, 128);
  bool recorded = Zutcp::TCPStateTest::record(*link, ref);
  Zutcp::TCPStateTest::peer(*link, 0);
  app.holdTx = true;
  Zutcp::TCPStateTest::peer(*link, 4);
  if (!scenario) {
    for (unsigned n = 8; n <= 128; n += 4) Zutcp::TCPStateTest::peer(*link, n);
    ZuCHECK(recorded && app.txJobs.count_() == 1 && !link->txAckd(),
      "32 advancing peer ACKs retain one notification and no packet queue");
  } else if (scenario == 1) {
    Zutcp::TCPStateTest::peerWriting(*link, true);
    if (auto job = app.txJobs.shift()) job();
    ZuCHECK(!link->txAckd() && !app.txJobs.count_() &&
        !Zutcp::TCPStateTest::peerPending(*link),
      "a writer overlap consumes no mixed snapshot and posts no spinning retry");
    Zutcp::TCPStateTest::peerWriting(*link, false);
    Zutcp::TCPStateTest::peer(*link, 128);
    ZuCHECK(app.txJobs.count_() == 1,
      "completing the Rx publication after the clear re-enrolls the last ACK");
  } else if (scenario == 2) {
    app.clockFn = [link = link.ptr()] { Zutcp::TCPStateTest::peer(*link, 128); };
    if (auto job = app.txJobs.shift()) job();
    ZuCHECK(link->txAckd() == 4 && app.txJobs.count_() == 1,
      "a new ACK during Tx processing enrolls after the old notification clears");
  } else if (scenario == 3) {
    auto old = app.txJobs.shift();
    app.holdTx = false;
    Zutcp::TCPStateTest::stage(*link, false);
    auto fresh = retained(replacement, pool, 128);
    recorded &= Zutcp::TCPStateTest::record(*link, fresh);
    Zutcp::TCPStateTest::peer(*link, 0);
    app.holdTx = true;
    Zutcp::TCPStateTest::peer(*link, 128);
    old();
    ZuCHECK(!link->txAckd() && Zutcp::TCPStateTest::peerPending(*link) &&
        app.txJobs.count_() == 1,
      "old-generation notification cannot clear or apply the replacement snapshot");
    fresh = {};
  } else {
    Zutcp::TCPStateTest::peer(*link, 64);
    app.runTx();
    Zutcp::TCPStateTest::peerNotify(*link);
    app.runTx();
    bool once = link->txAckd() == 64 && !Zutcp::TCPStateTest::duplicates(*link);
    for (unsigned i = 0; i < 3; ++i) Zutcp::TCPStateTest::peer(*link, 64);
    bool queued = app.txJobs.count_() == 3;
    app.runTx();
    ZuCHECK(once && queued && Zutcp::TCPStateTest::duplicates(*link) == 3 && link->queued,
      "snapshot deduplication invents no duplicate ACK; three wire duplicates still trigger recovery");
    Zutcp::TCPStateTest::peer(*link, 128);
  }
  app.runTx();
  ZuCHECK(recorded && link->txAckd() == 128 &&
      !Zutcp::TCPStateTest::records(*link) && !app.txJobs.count_() &&
      !Zutcp::TCPStateTest::peerPending(*link),
    "the solitary final cumulative ACK completes retirement without another packet or timer");
  link->finish(); link = nullptr; ref = {};
  ZuCHECK(source.valid && source.returned == 1 && replacement.valid &&
      replacement.returned == unsigned(scenario == 3),
    "coalescing and reset return each original backing exactly once");
}

static void peerAckWindow(unsigned scenario)
{
  ZuTestScopeRT(peerAckWindow);
  App app{ZuTime{0}, {}, false};
  ZmRef<Link> link = new Link{&app};
  Native source{app.mx()}; Zxdp::BufPool pool{1, sizeof(source.bytes)};
  auto ref = retained(source, pool, 128);
  bool recorded = Zutcp::TCPStateTest::record(*link, ref);
  Zutcp::TCPStateTest::peer(*link, 0);
  app.holdTx = true;
  Zutcp::TCPStateTest::peer(*link, 32);
  unsigned window = scenario ? 0 : 4096;
  Zutcp::TCPStateTest::peer(*link, 32, window, 1);
  Zutcp::TCPStateTest::peer(*link, 64, window, 1);
  if (scenario == 2) {
    window = 4096;
    Zutcp::TCPStateTest::peer(*link, 96, window, 2);
    Zutcp::TCPStateTest::peer(*link, 128, window, 2);
  }
  app.runTx();
  uint64_t ack = scenario == 2 ? 128 : 64;
  bool latest = link->txAckd() == ack &&
    Zutcp::TCPStateTest::peerLimit(*link) == ack + window;
  Zutcp::TCPStateTest::peer(*link, uint32_t(ack), 65535, 0);
  app.runTx();
  ZuCHECK(recorded && latest && Zutcp::TCPStateTest::peerLimit(*link) == ack + window,
    "shrink, zero and reopen survive newer cumulative snapshots; older sequence cannot expand credit");
  Zutcp::TCPStateTest::peer(*link, 128, window, 2);
  app.runTx();
  link->finish(); link = nullptr; ref = {};
  ZuCHECK(source.valid && source.returned == 1,
    "window exceptions retain exact-once final backing retirement");
}

static void peerAckWrap()
{
  ZuTestScope(peerAckWrap);
  App app{ZuTime{0}, {}, false};
  ZmRef<Link> link = new Link{&app};
  Native source{app.mx()}; Zxdp::BufPool pool{1, sizeof(source.bytes)};
  constexpr uint64_t base = (uint64_t(1) << 32) - 4;
  Zutcp::TCPStateTest::peerWrap(*link, base);
  auto ref = retained(source, pool, 8, base);
  bool recorded = Zutcp::TCPStateTest::record(*link, ref);
  Zutcp::TCPStateTest::peer(*link, uint32_t(base));
  app.holdTx = true;
  Zutcp::TCPStateTest::peer(*link, uint32_t(base + 2), 65535, 7,
    Zutcp::TCPOptions{.flags = Zutcp::TCPOptions::TS, .tsecr = 123});
  Zutcp::TCPStateTest::peer(*link, 4, 65535, 8,
    Zutcp::TCPOptions{.flags = Zutcp::TCPOptions::TS, .tsecr = 0});
  Zutcp::TCPHeader snapshot;
  bool coherent = Zutcp::TCPStateTest::peerSnapshot(*link, snapshot);
  ZuCHECK(recorded && coherent && app.txJobs.count_() == 1 && snapshot.seq == 8 &&
      snapshot.ack == 4 && snapshot.options.flags == Zutcp::TCPOptions::TS &&
      !snapshot.options.tsecr,
    "latest sequence, wrapped ACK and timestamp-present zero echo form one coherent snapshot");
  app.runTx();
  ZuCHECK(link->txAckd() == base + 8 && !Zutcp::TCPStateTest::records(*link),
    "Rx classification and Tx normalization preserve cumulative progress across 32-bit wrap");
  link->finish(); link = nullptr; ref = {};
  ZuCHECK(source.valid && source.returned == 1, "wrapped ACK retires its backing exactly once");
}

static void peerAckExceptions()
{
  ZuTestScope(peerAckExceptions);
  {
    App app{ZuTime{0}, {}, false};
    ZmRef<Link> link = new Link{&app};
    Native a{app.mx()}, b{app.mx()}, c{app.mx()};
    Zxdp::BufPool pool{3, sizeof(a.bytes)};
    auto first = retained(a, pool, 1), middle = retained(b, pool, 2, 1),
      last = retained(c, pool, 1, 3);
    bool recorded = Zutcp::TCPStateTest::record(*link, first) &&
      Zutcp::TCPStateTest::record(*link, middle) && Zutcp::TCPStateTest::record(*link, last);
    Zutcp::TCPStateTest::peer(*link, 0);
    app.holdTx = true;
    Zutcp::TCPStateTest::peer(*link, 1);
    Zutcp::TCPStateTest::peer(*link, 1, 65535, 0,
      Zutcp::TCPOptions{.sackCount = 2, .sack = {{0, 1}, {3, 4}}});
    Zutcp::TCPStateTest::peer(*link, 2);
    bool two = app.txJobs.count_() == 2;
    if (auto job = app.txJobs.shift()) job();
    bool latest = link->txAckd() == 2;
    app.runTx();
    ZuCHECK(recorded && two && latest && Zutcp::TCPStateTest::discounted(*link) == 1 &&
        Zutcp::TCPStateTest::dsackReports(*link) == 1,
      "new cumulative progress may precede an older report, whose higher SACK and DSACK remain effective");
    Zutcp::TCPStateTest::peer(*link, 4);
    app.runTx(); link->finish(); link = nullptr;
    first = {}; middle = {}; last = {};
    ZuCHECK(a.valid && b.valid && c.valid && a.returned == 1 && b.returned == 1 && c.returned == 1,
      "exceptional SACK processing and final cumulative progress retire backing once");
  }
  {
    App app{ZuTime{0}, {}, false};
    ZmRef<Link> link = new Link{&app};
    Native source{app.mx()}; Zxdp::BufPool pool{1, sizeof(source.bytes)};
    auto ref = retained(source, pool, 6);
    bool recorded = Zutcp::TCPStateTest::record(*link, ref);
    Zutcp::TCPStateTest::peer(*link, 0);
    app.holdTx = true;
    constexpr uint8_t flags[] = {0x12, 0x11, 0x14, 0x50, 0x90};
    for (unsigned i = 0; i < 5; ++i)
      Zutcp::TCPStateTest::peer(*link, i + 1, 65535, 0, {}, flags[i]);
    ZuCHECK(recorded && app.txJobs.count_() == 5 && !Zutcp::TCPStateTest::peerPending(*link),
      "advancing SYN, FIN, RST, ECE and CWR headers each retain the full event path");
    app.runTx();
    Zutcp::TCPStateTest::peer(*link, 6); app.runTx();
    link->finish(); link = nullptr; ref = {};
    ZuCHECK(source.valid && source.returned == 1, "full header exceptions preserve final retirement");
  }
  {
    App app{ZuTime{0}, {}, false};
    ZmRef<Link> link = new Link{&app};
    Native source{app.mx()}; Zxdp::BufPool pool{1, sizeof(source.bytes)};
    auto ref = retained(source, pool, 4); ref.fin = true;
    bool recorded = Zutcp::TCPStateTest::record(*link, ref);
    Zutcp::TCPStateTest::peer(*link, 0);
    app.holdTx = true;
    Zutcp::TCPStateTest::peer(*link, 5);
    bool one = app.txJobs.count_() == 1 && Zutcp::TCPStateTest::peerPending(*link);
    app.runTx();
    ZuCHECK(recorded && one && link->txAckd() == 5 && Zutcp::TCPStateTest::finDone(*link),
      "a pure cumulative ACK covering committed local FIN completes the real FIN obligation");
    link->finish(); link = nullptr; ref = {};
    ZuCHECK(source.valid && source.returned == 1, "FIN progress returns data backing once");
  }
}

static void peerAckBatch()
{
  ZuTestScope(peerAckBatch);
  App app{ZuTime{0}, {}, false};
  ZmRef<Link> link = new Link{&app};
  constexpr unsigned n = Zutcp::RecoveryScanBatch + 8;
  Zxdp::BufPool pool{n, 512};
  ZtArray<ZuPtr<Native>> sources;
  bool recorded = true;
  for (unsigned i = 0; i < n; ++i) {
    sources.push(ZuPtr<Native>{new Native{app.mx()}});
    auto ref = retained(*sources[i], pool, 1, i);
    recorded &= Zutcp::TCPStateTest::record(*link, ref);
  }
  Zutcp::TCPStateTest::peer(*link, 0);
  app.holdTx = true;
  Zutcp::TCPStateTest::peer(*link, n - 2);
  app.clockFn = [link = link.ptr()] { Zutcp::TCPStateTest::peer(*link, n); };
  if (auto job = app.txJobs.shift()) job();
  ZuCHECK(recorded && link->txAckd() == n - 2 && Zutcp::TCPStateTest::records(*link) &&
      app.txJobs.count_() == 2,
    "one cumulative ACK retains bounded retirement continuation while newer progress is notified");
  app.runTx();
  bool returned = true;
  for (const auto &source : sources) returned &= source->valid && source->returned == 1;
  ZuCHECK(link->txAckd() == n && !Zutcp::TCPStateTest::records(*link) && returned &&
      !app.txJobs.count_() && !Zutcp::TCPStateTest::peerPending(*link),
    "newer ACK before older retirement continuation releases every original exactly once");
  link->finish(); link = nullptr;
}

static void noCCAdmission()
{
  ZuTestScope(noCCAdmission);
  App app{ZuTime{0}, {}, false};
  ZmRef<Link> link = new Link{&app};
  Native sources[12]{{app.mx()}, {app.mx()}, {app.mx()}, {app.mx()},
    {app.mx()}, {app.mx()}, {app.mx()}, {app.mx()},
    {app.mx()}, {app.mx()}, {app.mx()}, {app.mx()}};
  Zxdp::BufPool pool{16, sizeof(sources[0].bytes)};
  Zutcp::SentFrameRef refs[12];
  for (unsigned i = 0; i < 12; ++i) refs[i] = retained(sources[i], pool, 200, i * 200);
  Zutcp::TCPStateTest::fastSetup(*link);
  Zutcp::TCPStateTest::pacingRTT(*link);
  app.clockCalls = 0;
  bool ready = Zutcp::TCPStateTest::pacingReady(*link);
  ZuCHECK(ready && !app.clockCalls && !app.ecn(),
    "disabled congestion control bypasses pacing without reading time and disables ECN");
  unsigned attempts = 0;
  auto accept = [&attempts](auto buf, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    ++attempts;
    buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
    return true;
  };
  bool refused = !link->output(refs[0], [&attempts](
      auto, const Zxdp::CommitFn &) { ++attempts; return false; });
  app.writable = false;
  bool busy = !link->output(refs[0], accept);
  app.writable = true;
  Zutcp::TCPStateTest::window(*link, 0);
  bool zero = !link->output(refs[0], accept);
  ZuCHECK(refused && busy && zero && attempts == 1 && !link->txNext() &&
      !Zutcp::TCPStateTest::records(*link),
    "disabled congestion control retains backend refusal, local credit and zero-window gates");
  Zutcp::TCPStateTest::window(*link, 1);
  bool first = link->output(refs[0], accept);
  bool window = !link->output(refs[1], accept);
  ZuCHECK(first && window && attempts == 2 && link->txNext() == 200,
    "a positive peer window admits one whole original but still fences its successor");
  Zutcp::TCPStateTest::window(*link, 65535);
  bool sent = true;
  for (unsigned i = 1; i < 12; ++i) sent &= link->output(refs[i], accept);
  auto diag = Zutcp::TCPStateTest::diag(*link);
#ifdef Zutcp_DEBUG
  bool diagnosticFlight = diag.bytesInFlight == 2400 && diag.pktBytesInFlight == 2400;
#else
  bool diagnosticFlight = !diag.bytesInFlight && !diag.pktBytesInFlight;
#endif
  ZuCHECK(sent && attempts == 13 && link->txNext() == 2400 &&
      Zutcp::TCPStateTest::records(*link) == 12 && !Zutcp::TCPStateTest::paced(*link) &&
      Zutcp::TCPStateTest::cwnd(*link) == 2000 && !Zutcp::TCPStateTest::flight(*link) &&
      Zutcp::TCPStateTest::packetFlight(*link) == 2400 &&
      diagnosticFlight && !diag.congestionWindow,
    "all eligible originals exceed initial cwnd immediately without pacing or NewReno debt; "
    "diagnostics retain actual recovery flight");
  app.clock += Zutcp::timeUS(10000);
  Zutcp::TCPStateTest::ack(*link, 100);
  bool partial = link->txAckd() == 100 && Zutcp::TCPStateTest::records(*link) == 12;
  Zutcp::TCPStateTest::ack(*link, 2400);
  ZuCHECK(partial && link->txAckd() == 2400 && !Zutcp::TCPStateTest::records(*link) &&
      !Zutcp::TCPStateTest::packetFlight(*link) &&
      !Zutcp::TCPStateTest::diag(*link).bytesInFlight &&
      Zutcp::TCPStateTest::cwnd(*link) == 2000 && *Zutcp::TCPStateTest::handshakeRTT(*link),
    "partial and cumulative ACK retirement and RTT sampling survive disabled growth");
  link->finish(); link = nullptr;
  bool returned = true;
  for (unsigned i = 0; i < 12; ++i) {
    refs[i] = {};
    returned &= sources[i].valid && sources[i].returned == 1;
  }
  ZuCHECK(returned, "disabled congestion control releases every acknowledged original once");
}

static void noCCRecovery(bool rto)
{
  ZuTestScopeRT(noCCRecovery);
  App app{ZuTime{0}, {}, false};
  ZmRef<Link> link = new Link{&app};
  Native sources[8]{{app.mx()}, {app.mx()}, {app.mx()}, {app.mx()},
    {app.mx()}, {app.mx()}, {app.mx()}, {app.mx()}};
  Zxdp::BufPool pool{8, sizeof(sources[0].bytes)};
  Zutcp::SentFrameRef refs[8];
  Zutcp::TCPStateTest::fastSetup(*link);
  bool setup = true;
  for (unsigned i = 0; i < 8; ++i) {
    refs[i] = retained(sources[i], pool, 200, i * 200);
    setup &= link->output(refs[i], [](auto buf, const Zxdp::CommitFn &commit) {
      if (!commit()) return false;
      buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
      return true;
    });
    app.clock += Zutcp::timeUS(1000);
  }
  unsigned attempts = 0;
  bool selected = true;
  auto retransmit = [native = link.ptr(), &attempts, &selected] {
    Zutcp::SentFrameRef ref;
    bool next = native->next(ref);
    selected &= next && !ref.offset && ref.length == 200;
    return next && native->recoveryOutput(ref, [&attempts](
	auto buf, const Zxdp::CommitFn &commit) {
      if (!commit()) return false;
      ++attempts;
      buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
      return true;
    });
  };
  if (rto) {
    link->recovery = retransmit;
    Zutcp::TCPStateTest::expireRTO(*link);
    bool first = attempts == 1 && Zutcp::TCPStateTest::backoff(*link) == 1 &&
      Zutcp::TCPStateTest::rtoDeadline(*link) > app.clock;
    Zutcp::TCPStateTest::expireRTO(*link);
    ZuCHECK(setup && selected && first && attempts == 2 &&
        Zutcp::TCPStateTest::backoff(*link) == 2 &&
        Zutcp::TCPStateTest::rtoDeadline(*link) > app.clock &&
        Zutcp::TCPStateTest::rtoIdle(*link),
      "disabled congestion control retains repeated RTO expiry, actual oldest retransmission "
      "and exponential backoff");
    link->recovery = {};
  } else {
    app.clock += Zutcp::timeUS(12000);
    bool lost = Zutcp::TCPStateTest::fastLoss(*link);
    bool sent = retransmit();
    ZuCHECK(setup && lost && selected && sent && attempts == 1 &&
        Zutcp::TCPStateTest::latest(*link, refs[0]) >= 8 &&
        !Zutcp::TCPStateTest::fastReady(*link),
      "SACK and RACK still select and retransmit the lost whole original without cwnd credit");
  }
  app.clock += Zutcp::timeUS(10000);
  Zutcp::TCPStateTest::ack(*link, 1600);
  ZuCHECK(link->txAckd() == 1600 && !Zutcp::TCPStateTest::records(*link) &&
      !*Zutcp::TCPStateTest::rtoDeadline(*link) && !Zutcp::TCPStateTest::backoff(*link) &&
      Zutcp::TCPStateTest::cwnd(*link) == 2000 && !Zutcp::TCPStateTest::flight(*link),
    "cumulative ACK retires retransmitted and untouched history and cancels recovery timeout "
    "without congestion growth or reduction");
  link->finish(); link = nullptr;
  bool returned = true;
  for (unsigned i = 0; i < 8; ++i) {
    refs[i] = {};
    returned &= sources[i].valid && sources[i].returned == 1;
  }
  ZuCHECK(returned, "loss recovery without congestion control preserves and drains exact backing");
}

static void earlyPolicy(unsigned policy)
{
  ZuTestScopeRT(earlyPolicy);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::syn(*link);
  app.policy = policy;
  unsigned sends = 0;
  Zutcp::TCPStateTest::clientOffer(*link, [&sends] { ++sends; });
  ZuCHECK(app.policyCalls == 1 && app.allowCalls == unsigned(policy != 1) &&
      sends == unsigned(policy == 0) && !link->txNext() &&
      !Zutcp::TCPStateTest::records(*link),
    "default-off policy still permits ordinary setup; abort inside either early hook stops "
    "setup");
  link->finish();
}

static void rtoQuota(unsigned scenario)
{
  ZuTestScopeRT(rtoQuota);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native sources[8]{{app.mx()}, {app.mx()}, {app.mx()}, {app.mx()},
    {app.mx()}, {app.mx()}, {app.mx()}, {app.mx()}};
  Zxdp::BufPool pool{8, sizeof(sources[0].bytes)};
  Zutcp::SentFrameRef refs[8];
  bool setup = true;
  for (unsigned i = 0; i < 8; ++i) {
    refs[i] = retained(sources[i], pool, 200, i * 200);
    setup &= Zutcp::TCPStateTest::record(*link, refs[i]);
  }
  if (scenario == 3) Zutcp::TCPStateTest::ack(*link, 100);
  Zutcp::TCPStateTest::exhaustCwnd(*link);
  unsigned attempts = 0;
  bool normal = link->output(refs[0], [&attempts](
      auto, const Zxdp::CommitFn &) { ++attempts; return false; });
  bool first = false, reentry = true, second = true, other = true;
  Zutcp::TCPStateTest::rtoAttempt(*link, [
    link, &refs, &attempts, &first, &reentry, &second, &other, scenario] {
    other = link->output(refs[1], [&attempts](
	auto, const Zxdp::CommitFn &) { ++attempts; return false; });
    first = link->output(refs[0], [link, &refs, &attempts, &reentry, scenario](
	auto buf, const Zxdp::CommitFn &commit) {
      if (!commit()) return false;
      ++attempts;
      reentry = link->output(refs[0], [&attempts](
	  auto, const Zxdp::CommitFn &) { ++attempts; return false; });
      if (scenario == 0) return false;
      buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
      if (scenario == 2) Zutcp::TCPStateTest::reset(*link);
      return true;
    });
    if (scenario != 0) second = link->output(refs[0], [&attempts](auto, const Zxdp::CommitFn &) {
      ++attempts; return false;
    });
  });
  bool accept = scenario != 0;
  ZuCHECK(setup && !normal && !other && !reentry && first == accept &&
      (!accept || !second) && attempts == 1 && Zutcp::TCPStateTest::rtoIdle(*link),
    "RTO alone permits one oldest whole original beyond cwnd; refusal and reentry preserve "
    "quota");
  if (scenario == 2) {
    ZuCHECK(!Zutcp::TCPStateTest::records(*link) && !link->txNext() && !link->txAckd(),
      "accepted old-generation output cannot publish history or frontiers into replacement "
      "state");
  } else {
    ZuCHECK(link->txNext() == 1600 && link->txAckd() == unsigned(scenario == 3 ? 100 : 0) &&
        Zutcp::TCPStateTest::latest(*link, refs[0]) == unsigned(accept ? 8 : 0),
      "accepted RTO chronology advances only the same canonical original; partial ACK never "
      "rebases it");
  }
  link->finish(); link = nullptr;
  bool returned = true;
  for (unsigned i = 0; i < 8; ++i) {
    refs[i] = {};
    returned &= sources[i].valid && sources[i].returned == 1;
  }
  ZuCHECK(returned, "RTO success, refusal and replacement drain each independent original once");
}

static void tlpChoice(unsigned scenario)
{
  ZuTestScopeRT(tlpChoice);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}, b{app.mx()}, c{app.mx()};
  Zxdp::BufPool pool{3, sizeof(a.bytes)};
  auto first = retained(a, pool), second = retained(b, pool, 64, 64);
  bool setup = Zutcp::TCPStateTest::record(*link, first) &&
    Zutcp::TCPStateTest::record(*link, second);
  if (scenario) {
    app.nativeAllocator = &c; app.nativePool = &pool;
    ZmRef<ZiIOBuf> queued = new Zi::IOBufStub<ZiIOBuf>{
      c.bytes, sizeof(c.bytes), &c, nullptr, 32, false, true};
    queued->skip = 256;
    setup &= link->enqueue_(ZuMv(queued));
  }
  if (scenario == 2) Zutcp::TCPStateTest::window(*link, 128);
  bool fallback = Zutcp::TCPStateTest::fireTLP(*link);
  ZuCHECK(setup && fallback && link->newCalls == unsigned(scenario == 1) &&
      link->retxCalls == unsigned(scenario != 1) &&
      (scenario == 1 || link->selectedOffset == 64) &&
      link->txRangeCount() == unsigned(bool(scenario)) && !a.returned && !b.returned,
    "TLP tries eligible new whole original first, otherwise highest outstanding, then rearms "
    "RTO");
  link->finish(); link = nullptr; first = second = {};
  ZuCHECK(a.valid && b.valid && c.valid && a.returned == 1 && b.returned == 1 &&
      c.returned == unsigned(bool(scenario)),
    "refused TLP selection retains exact producer and sent-original leases until owner drain");
}

static void timestampRTT(unsigned scenario)
{
  ZuTestScopeRT(timestampRTT);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  auto ref = retained(a, pool);
  bool timestamp = scenario < 3;
  bool repeated = scenario == 1;
  bool karn = scenario != 4;
  bool setup = Zutcp::TCPStateTest::timestampRecord(*link, ref, timestamp, repeated, karn);
  app.clock += Zutcp::timeUS(20000);
  Zutcp::TCPOptions echoed;
  if (timestamp) echoed = {.flags = Zutcp::TCPOptions::TS, .tsecr = scenario == 2 ? 8U : 7U};
  Zutcp::TCPStateTest::ack(*link, 64, echoed);
  auto sample = Zutcp::TCPStateTest::handshakeRTT(*link);
  bool expected = scenario == 0 || scenario == 4;
  ZuCHECK(setup && (*sample == expected) && (!expected || sample == Zutcp::timeUS(20000)) &&
      link->txAckd() == 64 && a.returned == 1,
    "unique accepted timestamp evidence lifts Karn; ambiguous/missing evidence preserves it");
  link->finish(); link = nullptr; ref = {};
  ZuCHECK(a.valid && a.returned == 1, "RTT histories never retain or release original twice");
}

static void boundedSACK(bool stale)
{
  ZuTestScopeRT(boundedSACK);
  App app;
  ZmRef<Link> link = new Link{&app};
  enum { Count = 260 };
  Native *sources[Count] = {};
  Zxdp::BufPool pool{512, 512};
  bool setup = true;
  for (unsigned i = 0; i < Count; ++i) {
    sources[i] = new Native{app.mx()};
    auto ref = retained(*sources[i], pool, 1, i);
    setup &= Zutcp::TCPStateTest::record(*link, ref);
  }
  bool probe = Zutcp::TCPStateTest::selectProbe(*link);
  app.holdTx = true;
  Zutcp::TCPOptions options;
  options.sackCount = 1; options.sack[0] = {1, Count};
  Zutcp::TCPStateTest::ack(*link, 0, options);
  auto partial = Zutcp::TCPStateTest::discounted(*link);
  ZuCHECK(setup && probe && partial > 0 && partial < Count - 1 && app.txJobs.count_() == 1 &&
      Zutcp::TCPStateTest::rtoSelected(*link),
    "bounded partial SACK yields one continuation and restores the RTO obligation before "
    "its final batch");
  if (stale) Zutcp::TCPStateTest::reset(*link);
  app.holdTx = false; app.runTx(); app.runRx(); app.runTx();
  if (stale) {
    ZuCHECK(!Zutcp::TCPStateTest::discounted(*link) && !Zutcp::TCPStateTest::records(*link) &&
        !link->txNext() && !link->txAckd() && !link->disconnecting(),
      "bounded SACK continuation cannot retain or alter replacement-generation history");
  } else {
    ZuCHECK(Zutcp::TCPStateTest::discounted(*link) == Count - 1 && !link->txAckd() &&
        !sources[0]->returned && !sources[Count - 1]->returned,
      "all continuation batches retain canonical originals until cumulative byte delivery");
    Zutcp::TCPStateTest::ack(*link, Count);
  }
  link->finish(); link = nullptr;
  bool returned = true;
  for (unsigned i = 0; i < Count; ++i) {
    returned &= sources[i]->valid && sources[i]->returned == 1;
    delete sources[i];
  }
  ZuCHECK(returned, "bounded SACK completion and replacement release each original lease once");
}

struct RxAttachAlloc : public Zi::IOBufAllocator {
  mutable unsigned returned = 0, heapReturned = 0;

  RxAttachAlloc() : Zi::IOBufAllocator{nullptr, Zi::IOBufType::RxTCP, 64, 8} { }
  uint8_t *alloc(unsigned size) const override {
    return Zutcp::inlineAllocator()->alloc(size);
  }
  void free(uint8_t *ptr, unsigned size, bool registered) const override {
    if (registered) ++returned;
    else {
      ++heapReturned;
      Zutcp::inlineAllocator()->free(ptr, size, false);
    }
  }
  ZmRef<ZiIOBuf> allocBuf(
    void *owner = nullptr, uint32_t length = 0, uint32_t = ~uint32_t(0)) override
  {
    ZmAssert_(!length);
    auto buf = new Zi::IOBufStub<ZiIOBuf, ZuStringT<"Zutcp.Rx.Attach">>{
      nullptr, 0, this, owner, 0, false, true};
    buf->skip = headRoom;
    return buf;
  }
};

static void testMxRxStub(bool grow, bool shared)
{
  ZuTestScope(testMxRxStub);
  RxAttachAlloc allocator;
  alignas(64) uint8_t bytes[64];
  for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = uint8_t(i ^ 0x73);
  ZmRef<ZiIOBuf> packet = new Zi::IOBufStub<ZiIOBuf, ZuStringT<"Zutcp.Rx.Packet">>{
    bytes, sizeof(bytes), &allocator, nullptr, sizeof(bytes), false, true};
  ZmRef<Zutcp::DataRxPQueue::Node> data = new Zutcp::DataRxPQueue::Node{
    packet, bytes + 8, 24, Zutcp::inlineAllocator(), nullptr, 0};
  unsigned owner = 0;
  ZiRxContext context;
  context.buf = allocator.allocBuf(&owner);
  auto stub = context.buf.ptr();
  auto logical = stub->allocator();
  if (!shared) packet = nullptr;
  ZuCHECK(data->attach(stub), "receive attachment must succeed");
  data = nullptr;
  packet = nullptr;
  ZuCHECK(context.buf.ptr() == stub && stub->owner == &owner &&
      stub->length == 24 && stub->allocator() == logical &&
      (shared ? !stub->registered() && allocator.returned == 1 :
	stub->data_() == bytes && stub->data() == bytes + 8 &&
	stub->size == sizeof(bytes) && stub->registered() && !allocator.returned),
    "registered receive attachment must preserve caller metadata and source bytes");
  if (grow) {
    ZuCHECK(stub->ensure(80) && !stub->registered() &&
	stub->allocator() == &allocator && allocator.returned == 1,
      "heap growth must restore logical allocator and release the received packet");
    memset(bytes, 0x99, sizeof(bytes));
    bool exact = true;
    for (unsigned i = 0; i < 24; ++i) exact &= stub->data()[i] == uint8_t((i + 8) ^ 0x73);
    ZuCHECK(exact && stub->owner == &owner && stub->length == 24,
      "heap replacement must preserve the delivered view after source reuse");
  }
  auto alias = context.buf;
  context.buf = nullptr;
  ZuCHECK(allocator.returned == unsigned(grow || shared),
    "shared caller metadata must keep original receive storage until final release");
  alias = nullptr;
  ZuCHECK(allocator.returned == 1 && allocator.heapReturned == unsigned(grow || shared),
    "final receive release must return original or replacement backing once");
}

class MxRxCxn : public ZmNHeap<MxRxCxn>, public ZiConnection {
friend inline ZuStringT<"Zutcp.Test.MxRx"> ZmNHeapID(MxRxCxn *);
public:
  using ZmNHeap<MxRxCxn>::operator new;
  using ZmNHeap<MxRxCxn>::operator delete;
  MxRxCxn(ZiMultiplex *mx) :
    ZiConnection{mx, {.type = ZiCxnType::TCPOut, .socket = Zi::nullSocket()}} { }
  void connected(ZiRxContext &) override { }
  void disconnected(bool) override { }
};

void testMxRxDelivery(bool reentrant)
{
  ZuTestScope(testMxRxDelivery);
  App app;
  ZmRef<Link> cxn = new Link{&app};
  ZmRef<MxRxCxn> mx = new MxRxCxn{app.mx()};
  RxAttachAlloc allocator;
  unsigned calls = 0, depth = 0, maxDepth = 0;
  bool exact = true;
  ZmRef<ZiIOBuf> held;
  ZmBlock<>{}([&app, cxn, mx, &allocator, &calls, &depth, &maxDepth,
      &exact, &held, reentrant](auto wake) mutable {
    app.mx()->run([&app, cxn, mx, &allocator, &calls, &depth, &maxDepth,
	&exact, &held, reentrant, wake = ZuMv(wake)]() mutable {
      cxn->mxCxn = mx;
      mx->plugin(static_cast<Link::Base *>(cxn.ptr()));
      auto &context = mx->rxContext();
      context.buf = allocator.allocBuf(cxn.ptr());
      exact &= cxn->receive(input(app, "world", 5)) && calls == 0 &&
        !context.buf->length;
      exact &= cxn->receive(input(app, "hello")) && calls == 0 &&
        !context.buf->length && cxn->reader().length() == 10;
      context.init(ZiRxFn{[cxn = cxn.ptr(), &allocator, &calls, &depth,
	  &maxDepth, &exact, &held, reentrant](ZiRxContext &context) {
	++depth;
	if (depth > maxDepth) maxDepth = depth;
	++calls;
	exact &= context.buf->allocator() == &allocator;
	if (calls == 1) {
	  exact &= context.buf->length == 5 &&
	    !memcmp(context.buf->data(), "hello", 5);
	  if (reentrant) {
	    held = ZuMv(context.buf);
	    context.buf = allocator.allocBuf(cxn);
	    static_cast<Link::Base *>(context.cxn->plugin())->recvMx();
	  }
	} else {
	  const char *expected = reentrant ? "world" : "helloworld";
	  unsigned length = reentrant ? 5 : 10;
	  exact &= context.buf->length == length &&
	    !memcmp(context.buf->data(), expected, length);
	}
	--depth;
	if (calls == 2) context.complete();
	return true;
      }}, ZuMv(context.buf));
      exact &= static_cast<Link::Base *>(mx->plugin())->recvMx() == 1 &&
	calls == 1;
      // True stops this delivery turn. The queued suffix must resume itself
      // without another receive, preserving the current stub or partial frame.
      cxn->app()->mx()->run([wake = ZuMv(wake)]() mutable { wake(); },
	cxn->app()->mx()->rxThread());
    }, app.mx()->rxThread());
  });
  ZuCHECK(exact && calls == 2 && maxDepth == 1 && !depth &&
      mx->rxBytes() == 10 && mx->rxCalls() == 2 && cxn->rxRetired() == 10 &&
      cxn->reader().empty(),
    "mx receive exact=", exact, " calls=", calls, " maxDepth=", maxDepth,
    " depth=", depth, " bytes=", mx->rxBytes(), " rxCalls=", mx->rxCalls(),
    " retired=", cxn->rxRetired(), " queued=", cxn->reader().length());
  ZuCHECK(!reentrant || (held && held->length == 5 &&
      !memcmp(held->data(), "hello", 5)),
    "context replacement must preserve backing handed to the application");
  held = nullptr;
  cxn->mxCxn = nullptr;
  mx->plugin(nullptr);
  mx->rxContext().complete();
  cxn->finish();
}

static void testMxRxRegistered()
{
  ZuTestScope(testMxRxRegistered);
  App app;
  ZmRef<Link> cxn = new Link{&app};
  ZmRef<MxRxCxn> mx = new MxRxCxn{app.mx()};
  RxAttachAlloc allocator;
  alignas(64) uint8_t bytes[64];
  memcpy(bytes + 8, "native-mx-byte-path", 19);
  bool exact = false;
  ZmBlock<>{}([&app, cxn, mx, &allocator, &bytes, &exact](auto wake) {
    app.mx()->rxRun([cxn, mx, &allocator, &bytes, &exact, wake = ZuMv(wake)]() mutable {
      cxn->mxCxn = mx;
      mx->plugin(static_cast<Link::Base *>(cxn.ptr()));
      mx->rxContext().init(ZiRxFn{[&allocator, &bytes, &exact](ZiRxContext &io) {
	exact = io.buf->registered() && io.buf->data_() == bytes &&
	  io.buf->data() == bytes + 8 && io.buf->length == 19 && !allocator.returned;
	io.complete();
	return true;
      }}, allocator.allocBuf(cxn.ptr()));
      auto d = input(*cxn->app(), {});
      d.buf = new Zi::IOBufStub<ZiIOBuf>{
	bytes, sizeof(bytes), &allocator, nullptr, 19, false, true};
      d.buf->skip = 8;
      exact = cxn->receive(ZuMv(d)) && exact;
      wake();
    });
  });
  ZuCHECK(exact && allocator.returned == 1 && !allocator.heapReturned,
    "live receiveSegment must transfer its unique ingress backing without a payload copy");
  cxn->mxCxn = nullptr; mx->plugin(nullptr);
  cxn->finish();
}

static void contiguousHeldBacking(bool refuse)
{
  ZuTestScope(contiguousHeldBacking);
  App app;
  RxAttachAlloc allocator;
  alignas(64) uint8_t first[64], second[64];
  memcpy(first + 8, "abc", 3);
  memcpy(second + 8, "def", 3);
  ZmRef<Link> link = new Link{&app};
  link->ready = false;
  app.allowRx = !refuse;
  auto a = input(app, {});
  a.buf = new Zi::IOBufStub<ZiIOBuf>{
    first, sizeof(first), &allocator, nullptr, 3, false, true};
  a.buf->skip = 8;
  bool firstAccepted = link->receive(ZuMv(a));
  ZuCHECK(firstAccepted && link->rxBytes() == 3 &&
      link->rxAdmitted() == 3 && link->reader().length() == 3 &&
      !link->rxPending() && link->rxCreditUsed() == 3 && app.demands == 1 &&
      allocator.returned == unsigned(refuse) &&
      (refuse ? link->reader().span().data() != first + 8 :
	link->reader().span().data() == first + 8),
    "first contiguous input must retain registered storage or its service copy");
  auto b = input(app, {}, 3);
  b.buf = new Zi::IOBufStub<ZiIOBuf>{
    second, sizeof(second), &allocator, nullptr, 3, false, true};
  b.buf->skip = 8;
  bool secondAccepted = link->receive(ZuMv(b));
  ZuCHECK(secondAccepted && link->rxBytes() == 6 &&
      link->rxAdmitted() == 6 && !link->rxRetired() &&
      link->reader().length() == 6 && !link->rxPending() &&
      link->rxCreditUsed() == 6 && app.demands == 2 && !link->processed &&
      allocator.returned == (refuse ? 2u : 0u) &&
      link->closedSnapshotRx(link->closedKeyRx()).rxNext == 2007,
    "next contiguous input must advance the ACK frontier while Reader holds its prefix");
  if (refuse) {
    memset(first, 0x91, sizeof(first));
    memset(second, 0x92, sizeof(second));
  }
  uint8_t bytes[6];
  unsigned copied = link->reader().copy(0, {bytes, sizeof(bytes)});
  auto retired = link->reader().advance(copied);
  bool released = link->retireRx(retired);
  ZuCHECK(copied == sizeof(bytes) && !memcmp(bytes, "abcdef", sizeof(bytes)) &&
      retired == 6 && released && link->reader().empty() &&
      link->rxRetired() == 6 && allocator.returned == 2,
    "both retained ingress slices or service copies must deliver exact bytes and release once");
  link->finish(); link = nullptr;
  ZuCHECK(allocator.returned == 2,
    "connection teardown must not return already retired ingress storage again");
}

static void contiguousQueued(bool prefix)
{
  ZuTestScope(contiguousQueued);
  App app;
  ZmRef<Link> link = new Link{&app};
  link->ready = false;
  app.holdRx = true;
  if (prefix) {
    ZuCHECK(link->receive(input(app, "abc")), "held prefix must be accepted");
  }
  link->startQueuing();
  bool accepted = prefix ? link->receive(input(app, "def", 3)) :
    link->receive(input(app, "abc"));
  ZuCHECK(accepted && link->rxPending() == 1 &&
      link->rxBytes() == (prefix ? 3u : 0u) &&
      link->reader().length() == (prefix ? 3u : 0u) &&
      link->closedSnapshotRx(link->closedKeyRx()).rxNext == (prefix ? 2007u : 2004u),
    "Rx::rcvd queue insertion must advance the ACK frontier before dequeue");
  if (!prefix) {
    ZuCHECK(link->receive(input(app, "def", 3)),
      "contiguous input behind an undelivered queue must be accepted");
  }
  ZuCHECK(link->rxAdmitted() == 6 && link->rxCreditUsed() == 6 &&
      app.demands == 2 && link->rxPending() == (prefix ? 1u : 2u) &&
      link->closedSnapshotRx(link->closedKeyRx()).rxNext == 2007,
    "queued contiguous input must preserve admission credit and the cumulative frontier");
  link->stopQueuing(prefix ? 3 : 0);
  ZuCHECK(link->reader().length() == (prefix ? 3u : 0u),
    "scheduled dequeue must leave Reader unchanged until its owner runs");
  app.holdRx = false;
  app.runRx(); app.runTx(); app.runRx();
  ZuCHECK(link->rxBytes() == 6 && link->rxAdmitted() == 6 &&
      link->reader().length() == 6 && !link->rxPending() &&
      !link->processed && link->closedSnapshotRx(link->closedKeyRx()).rxNext == 2007,
    "dequeue must join the held prefix without duplicate admission or frontier regression");
  uint8_t bytes[6];
  unsigned copied = link->reader().copy(0, {bytes, sizeof(bytes)});
  auto retired = link->reader().advance(copied);
  bool released = link->retireRx(retired);
  ZuCHECK(copied == sizeof(bytes) && !memcmp(bytes, "abcdef", sizeof(bytes)) &&
      retired == 6 && released && link->reader().empty() && link->rxRetired() == 6,
    "queued contiguous slices must drain as exact application bytes");
  link->finish();
}

static void mappedTxQueue()
{
  ZuTestScope(mappedTxQueue);
  App app;
  ZmRef<Link> cxn = new Link{&app};
  ZmRef<MxRxCxn> mx = new MxRxCxn{app.mx()};
  Native a{app.mx()}, b{app.mx()}; Zxdp::BufPool pool{2, sizeof(a.bytes)};
  app.nativeAllocator = &a; app.nativeAllocator2 = &b; app.nativePool = &pool;
  auto make = [](Native &a, ZuBSpan bytes) -> ZmRef<ZiIOBuf> {
    memcpy(a.bytes + 256, bytes.data(), bytes.length());
    auto buf = ZmRef<ZiIOBuf>{new Zi::IOBufStub<ZiIOBuf>{
      a.bytes, sizeof(a.bytes), &a, nullptr, unsigned(bytes.length()), false, true}};
    buf->skip = 256;
    return buf;
  };
  auto first = make(a, "one"), second = make(b, "xtwo");
  cxn->mxCxn = mx; mx->plugin(static_cast<Link::Base *>(cxn.ptr()));
  cxn->txQueueMax(1);
  unsigned calls = 0;
  bool exact = true;
  auto &context = mx->txContext();
  context.init(ZiTxFn{[native = cxn.ptr(), second, &calls, &exact](ZiTxContext &context) {
    auto target = native;
    ++calls;
    exact &= context.length == 3 && !context.offset;
    ZiTxFn complete{[&calls, &exact](ZiTxContext &context) {
      ++calls;
      exact &= context.length == 3 && context.offset == 1;
      context.complete();
      return true;
    }};
    auto next = second;
    context.init(ZuMv(complete), ZuMv(next)); context.offset = 1;
    target->sendMx(); // Context replacement has released the original callback.
    return true;
  }}, first);
  cxn->sendMx();
  bool pending = calls == 1 && !context.completed() && context.buf == second &&
    context.offset == 1 && !context.length && cxn->txQueueCount() == 1;
  cxn->dequeued = [native = cxn.ptr()] { native->sendMx(); };
  Zutcp::TxRange sent, next;
  bool dequeued = cxn->dequeueTxRange(sent);
  app.runTx(); // the same-owner continuation runs after packetization
  bool resumed = dequeued && calls == 2 && context.completed() && cxn->txQueueCount() == 1 &&
    cxn->txRange(0, next) && next.length == 3 && next.dataOffset == 3 &&
    next.storage->data + next.offset == b.bytes + 257 &&
    !memcmp(next.storage->data + next.offset, "two", 3) &&
    mx->txBytes() == 6 && mx->txCalls() == 2;
  auto empty = Zutcp::inlineAllocator()->allocBuf();
  context.init(ZiTxFn{[&exact](ZiTxContext &context) {
    exact &= !context.length;
    context.complete();
    return true;
  }}, empty);
  cxn->sendMx();
  bool zero = context.completed() && mx->txBytes() == 6 && mx->txCalls() == 2;
  ZuCHECK(pending && resumed && exact && zero,
    "mapped Tx queue pressure preserves replacement context/offset and resumes exact local "
    "counts");
  cxn->dequeued = {};
  cxn->mxCxn = nullptr; mx->plugin(nullptr); context.complete();
  cxn->finish(); cxn = nullptr;
  sent = next = {}; first = second = {};
  ZuCHECK(a.valid && b.valid && a.returned == 1 && b.returned == 1,
    "registered mapped producer originals survive metadata replacement and drain once");
}

static void oldIncarnationACK()
{
  ZuTestScope(oldIncarnationACK);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::timestamps(*link, 100);
  auto d = input(app, "old");
  d.tcp.ack = 1000; // Before this owner's accepted local SYN sequence space.
  d.tcp.options = {.flags = Zutcp::TCPOptions::TS, .tsval = 101};
  bool live = link->acceptsLiveTCP(d);
  bool received = link->receive(ZuMv(d));
  ZuCHECK(live && received && !link->rxAdmitted() && !link->rxBytes() &&
      !app.demands &&
      !link->disconnecting() && Zutcp::TCPStateTest::recent(*link) == 100,
    "overlapping live SEQ/new TS cannot admit stale-incarnation DATA with an invalid pre-SYN "
    "ACK");
  link->finish();
}

static void sequenceCycle(bool timestamps)
{
  ZuTestScopeRT(sequenceCycle);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::receiveCycle(*link);
  if (timestamps) Zutcp::TCPStateTest::timestamps(*link, 100);
  auto d = input(app, "alias");
  d.tcp.options = {.flags = Zutcp::TCPOptions::TS, .tsval = 99};
  bool accepted = link->acceptsLiveTCP(d);
  ZuCHECK(accepted == !timestamps,
    "full sequence-cycle alias is explicit without timestamps; negotiated old TS rejects it");
  link->finish();
}

static void receiveCE()
{
  ZuTestScope(receiveCE);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::TCPStateTest::ecn(*link);
  auto ce = input(app, "one"); ce.ecn = Zutcp::EcnMark::CE;
  bool first = link->receive(ZuMv(ce));
  Zutcp::TCPHeader firstACK, secondACK, clearedACK, renewedACK;
  bool ack1 = link->output({}, [&firstACK](
      auto buf, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    firstACK = buf->tcp;
    return true;
  });
  bool ack2 = link->output({}, [&secondACK](
      auto buf, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    secondACK = buf->tcp;
    return true;
  });
  bool cwr = link->receive(input(app, {}, 3, 0, 0x90));
  bool ack3 = link->output({}, [&clearedACK](
      auto buf, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    clearedACK = buf->tcp;
    return true;
  });
  auto renewed = input(app, "two", 3, 0, 0x90); renewed.ecn = Zutcp::EcnMark::CE;
  bool both = link->receive(ZuMv(renewed));
  bool ack4 = link->output({}, [&renewedACK](
      auto buf, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    renewedACK = buf->tcp;
    return true;
  });
  unsigned firstBytes = consume(*link);
  unsigned secondBytes = consume(*link);
  unsigned total = firstBytes + secondBytes;
  bool retired = link->retireRx(total);
  ZuCHECK(first && ack1 && ack2 && cwr && ack3 && both && ack4 &&
      (firstACK.flags & 0x40) && (secondACK.flags & 0x40) && !(clearedACK.flags & 0x40) &&
      (renewedACK.flags & 0x40) && firstBytes == 3 && secondBytes == 3 && total == 6 && retired,
    "CE echo remains sticky until current CWR; CWR with fresh CE starts a new sticky episode");
  link->finish();
}

static void originalCap(unsigned kind)
{
  ZuTestScopeRT(originalCap);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  app.nativeAllocator = &a; app.nativePool = &pool;
  Zutcp::SentFrameRef retainedRef;
  bool setup = true;
  if (kind == 2) {
    retainedRef = retained(a, pool, 200);
    setup = Zutcp::TCPStateTest::record(*link, retainedRef);
  } else if (kind) {
    auto buf = ZmRef<ZiIOBuf>{new Zi::IOBufStub<ZiIOBuf>{
      a.bytes, sizeof(a.bytes), &a, nullptr, 200, false, true}};
    buf->skip = 256;
    app.holdTx = kind == 3;
    setup = kind == 3 ? link->enqueue(ZuMv(buf)) : link->enqueue_(ZuMv(buf));
  }
  // Unpublished originals still need independent protection; published promises
  // also cover producer buffers which have not crossed their owner handoff yet.
  if (kind == 1 || kind == 2) Zutcp::TCPStateTest::privateBudget(*link);
  bool failed = !Zutcp::TCPStateTest::cap(*link, 150);
  bool closed = link->txAdmissionClosed() && !link->dataMPS();
  bool pending = kind != 3 || (link->txQueueCount() == 1 && !a.returned);
  bool repeated = !Zutcp::TCPStateTest::cap(*link, 150);
#ifdef Zutcp_DEBUG
  bool once = Zutcp::TCPStateTest::failures(*link) == 1;
#else
  bool once = true; // release diagnostics intentionally compile out
#endif
  app.holdTx = false; app.runTx(); app.runRx(); app.runTx();
  ZuCHECK(setup && failed && closed && pending && repeated && once &&
      (kind != 3 || (!link->txQueueCount() && a.returned == 1)),
    "cap invalidation idempotently closes admission and protects every original",
    " kind=", kind, " setup=", setup, " failed=", failed, " closed=", closed,
    " pending=", pending, " repeated=", repeated, " once=", once,
    " queued=", link->txQueueCount(), " returned=", a.returned);
  link->finish(); link = nullptr;
  if (kind == 2) {
    ZuCHECK(!a.returned && retainedRef.range.storage->data == a.bytes,
      "independent original reference survives the connection's budget-failure drain");
    retainedRef = {};
  }
  ZuCHECK(a.valid && a.returned == unsigned(kind != 0),
    "final original owner returns its canonical lease exactly once after cap failure");
}

static void resetQualification()
{
  ZuTestScope(resetQualification);
  for (unsigned scenario = 0; scenario < 2; ++scenario) {
    App app;
    ZmRef<Link> link = new Link{&app};
    Zutcp::TCPStateTest::timestamps(*link, 100);
    auto d = input(app, {}, scenario ? 3 : 0, 0, 0x04);
    bool live = link->acceptsLiveTCP(d);
    auto outside = input(app, {}, 4096, 0, 0x04);
    bool rejected = !link->acceptsLiveTCP(outside);
    bool ignored = link->receive(ZuMv(outside)) && !link->disconnecting() && !link->resets;
    bool reset = link->receive(ZuMv(d));
    ZuCHECK(live && rejected && ignored && reset && link->disconnecting() &&
        link->txAdmissionClosed() && !link->resets && !link->rxAdmitted() && !app.demands,
      "window-qualified RST without TS aborts; "
      "outside-window RST is silently ignored without echo");
    link->finish();
  }
}

static void fastRecovery(unsigned scenario)
{
  ZuTestScopeRT(fastRecovery);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native sources[9]{{app.mx()}, {app.mx()}, {app.mx()}, {app.mx()}, {app.mx()},
    {app.mx()}, {app.mx()}, {app.mx()}, {app.mx()}};
  Zxdp::BufPool pool{16, sizeof(sources[0].bytes)};
  Zutcp::SentFrameRef refs[8];
  Zutcp::TCPStateTest::fastSetup(*link);
  bool setup = true;
  for (unsigned i = 0; i < 8; ++i) {
    refs[i] = retained(sources[i], pool, 200, i * 200);
    setup &= Zutcp::TCPStateTest::record(*link, refs[i]);
    app.clock += Zutcp::timeUS(1000);
  }
  app.nativeAllocator = &sources[8]; app.nativePool = &pool;
  auto queued = ZmRef<ZiIOBuf>{new Zi::IOBufStub<ZiIOBuf>{
    sources[8].bytes, sizeof(sources[8].bytes), &sources[8], nullptr, 200, false, true}};
  queued->skip = 256;
  setup &= link->enqueue_(ZuMv(queued));
  Zutcp::TxRange fresh;
  setup &= link->txRange(0, fresh);
  auto newData = Zutcp::SentFrameRef::data(fresh, false);
  app.clock += Zutcp::timeUS(12000);
  setup &= Zutcp::TCPStateTest::fastLoss(*link);
  Zutcp::SentFrameRef candidate;
  setup &= link->next(candidate) && candidate.offset == 0 && candidate.length == 200;
  unsigned attempts = 0;
  bool ordinary = link->output(candidate, [&attempts](
      auto, const Zxdp::CommitFn &) { ++attempts; return false; });
  bool newAllowed = link->recoveryOutput(newData,
    [&attempts](auto, const Zxdp::CommitFn &) { ++attempts; return false; });
  bool reentry = true, direct = true, accepted = false;
  auto accept = [native = link.ptr(), &candidate, &attempts, &reentry, &direct, scenario](
      auto buf, const Zxdp::CommitFn &commit) {
    ++attempts;
    reentry = native->recoveryOutput(candidate,
      [&attempts](auto, const Zxdp::CommitFn &) { ++attempts; return false; });
    direct = native->output(candidate, [&attempts](
	auto, const Zxdp::CommitFn &) { ++attempts; return false; });
    if (!commit()) return false;
    buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
    if (scenario == 3) Zutcp::TCPStateTest::reset(*native);
    return true;
  };
  bool refused = true, retainedPermission = true;
  if (scenario == 2 || scenario == 7) {
    if (scenario == 2) Zutcp::TCPStateTest::delayRecovery(*link);
    else Zutcp::TCPStateTest::window(*link, 0);
    refused = !link->recoveryOutput(candidate,
      [&attempts](auto, const Zxdp::CommitFn &) { ++attempts; return false; });
    retainedPermission = Zutcp::TCPStateTest::fastReady(*link) && !attempts;
    setup &= Zutcp::TCPStateTest::requeue(*link, candidate);
    if (scenario == 2) {
      link->recovery = [native = link.ptr(), &candidate, &accepted, accept]() mutable {
	if (!native->next(candidate)) return false;
	accepted = native->recoveryOutput(candidate, accept);
	return accepted;
      };
      Zutcp::TCPStateTest::firePace(*link);
      link->recovery = {};
    } else {
      Zutcp::TCPStateTest::window(*link, 65535);
      setup &= link->next(candidate);
      accepted = link->recoveryOutput(candidate, accept);
    }
  } else {
    if (scenario == 1 || scenario >= 4) {
      refused = !link->recoveryOutput(candidate,
	[&attempts](auto, const Zxdp::CommitFn &) { ++attempts; return false; });
      retainedPermission = Zutcp::TCPStateTest::fastReady(*link) &&
	Zutcp::TCPStateTest::latest(*link, candidate) == 0;
    }
    if (scenario == 4) Zutcp::TCPStateTest::ack(*link, 1600);
    else if (scenario == 5) Zutcp::TCPStateTest::expireRTO(*link);
    else if (scenario == 6) Zutcp::TCPStateTest::exhaustCwnd(*link);
    if (scenario != 5) accepted = link->recoveryOutput(candidate, accept);
  }
  bool invalidated = scenario >= 4 && scenario <= 6;
  unsigned expectedAttempts = scenario == 1 ? 2 : 1;
  bool done = !Zutcp::TCPStateTest::fastReady(*link) &&
    Zutcp::TCPStateTest::fastIdle(*link);
  ZuCHECK(setup && !ordinary && !newAllowed && refused && retainedPermission &&
      accepted == !invalidated && attempts == expectedAttempts && done &&
      (invalidated || (!reentry && !direct)),
    "RACK first-original permission survives refusal/pacing/window, "
    "accepts once, and fences reentry");
  if (scenario == 3)
    ZuCHECK(!link->txNext() && !Zutcp::TCPStateTest::records(*link),
      "old accepted fast send cannot publish into a replacement generation");
  link->finish(); link = nullptr; candidate = {}; newData = {}; fresh = {};
  bool returned = true;
  for (unsigned i = 0; i < 8; ++i) refs[i] = {};
  for (auto &source : sources) returned &= source.valid && source.returned == 1;
  ZuCHECK(returned, "fast recovery preserves every canonical original through owner drain");
}

static void readiness()
{
  ZuTestScope(readiness);
  App app;
  ZmRef<Link> link = new Link{&app};
  link->ready = false;
  bool received = link->receive(input(app, "ready"));
  ZuCHECK(received && !link->processed && link->reader().length() == 5 && link->rxBytes() == 5,
    "ordinary TCP bytes stay in the Reader before application readiness");
  link->ready = true; link->resume();
  ZuCHECK(link->processed == 1 && consume(*link) == 5 && link->retireRx(5),
    "post-connected readiness resumes exact retained bytes without a new queue");
  link->finish();
}

static void finACKBeforeDequeue()
{
  ZuTestScope(finACKBeforeDequeue);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::SentFrameRef local;
  local.kind = Zutcp::SentFrameKind::Data; local.fin = true;
  bool setup = Zutcp::TCPStateTest::record(*link, local);
  app.holdRx = true;
  bool peer = link->receive(input(app, "nd", 1, 1, 0x11)) &&
    link->receive(input(app, "e", 0, 1, 0x10));
  ZmQueue<ZmFn<>> delayed;
  while (auto job = app.rxJobs.shift()) delayed.push(ZuMv(job));
  bool blocked = delayed.count_() && link->rxBytes() == 1 && !link->rxComplete() &&
    link->reader().length() == 1 && Zutcp::TCPStateTest::queuedFIN(*link);
  app.holdRx = false;
  unsigned attempts = 0;
  bool accepted = link->output({}, [&attempts](
      auto, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    ++attempts;
    return true;
  });
  ZuCHECK(setup && peer && blocked && accepted && attempts == 1 &&
      !link->finishes && !link->completions,
    "accepted contiguous FIN ACK precedes blocked Reader dequeue without completing EOF");
  unsigned first = consume(*link);
  bool prefix = first == 1 && link->retireRx(first) && !link->finishes;
  while (auto job = delayed.shift()) job();
  unsigned n = consume(*link);
  bool retired = prefix && n == 2 && link->retireRx(n);
  ZuCHECK(retired && link->finishes == 1 && link->completions == 1 && attempts == 1,
    "dequeue and Reader retirement reuse the accepted FIN ACK without another wire output");
  link->finish();
}

static void finACK(bool heldReader, bool stale)
{
  ZuTestScopeRT(finACK);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::SentFrameRef local;
  local.kind = Zutcp::SentFrameKind::Data; local.fin = true;
  bool setup = Zutcp::TCPStateTest::record(*link, local);
  bool peer = link->receive(input(app, "end", 0, 1, 0x11));
  if (!heldReader) { unsigned n = consume(*link); setup &= n == 3 && link->retireRx(n); }
  unsigned attempts = 0;
  bool refused = link->output({}, [&attempts](
      auto, const Zxdp::CommitFn &) { ++attempts; return false; });
  ZuCHECK(setup && peer && !refused && !link->finishes && !link->completions,
    "refused final ACK cannot finish native retirement or queued disconnect callbacks");
  app.holdRx = stale;
  bool accepted = link->output({}, [&attempts](
      auto, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    ++attempts;
    return true;
  });
  if (stale) {
    Zutcp::TCPStateTest::reset(*link);
    app.holdRx = false; app.runRx();
    ZuCHECK(accepted && !link->finishes && !link->completions,
      "old accepted final-ACK handoff cannot finish a replacement generation");
  } else if (heldReader) {
    ZuCHECK(accepted && !link->finishes && !link->completions,
      "accepted ACK alone cannot finish while the application still holds its Reader");
    unsigned n = consume(*link);
    bool retired = n == 3 && link->retireRx(n);
    ZuCHECK(retired && link->finishes == 1 && link->completions == 1,
      "accepted ACK plus reader retirement finishes native I/O and disconnect callback once");
  } else {
    ZuCHECK(accepted && link->finishes == 1 && link->completions == 1,
      "accepted final ACK completes the existing native graceful-retirement joins exactly once");
  }
  link->finish();
}

static void rxReadCommon()
{
  ZuTestScope(rxReadCommon);
  App app;
  ZmRef<Link> link = new Link{&app};
  app.holdTx = true;
  Zutcp::TCPStateTest::timestamps(*link, 0);
  Zutcp::TCPStateTest::advanceRx(*link, 7);
  auto zeroOptions = Zutcp::TCPStateTest::sampledOptions(*link);
  auto zero = Zutcp::TCPStateTest::header(*link);
  Zutcp::TCPStateTest::timestamps(*link, 31);
  Zutcp::TCPStateTest::advanceRx(*link, 9);
  auto latestOptions = Zutcp::TCPStateTest::sampledOptions(*link);
  auto latest = Zutcp::TCPStateTest::header(*link);
  ZuCHECK(!app.txJobs.count_() && zero.ack == 2008 && latest.ack == 2010 &&
      (zeroOptions.flags & Zutcp::TCPOptions::TS) && !zeroOptions.tsecr &&
      latestOptions.tsecr == 31 && !latest.options.sackCount && latest.window,
    "ordinary Rx publication exposes latest ACK/window and TS zero without a Tx job");
  Zutcp::TCPStateTest::reset(*link);
  auto stale = Zutcp::TCPStateTest::header(*link);
  ZuCHECK(!stale.flags,
    "new Rx generation cannot be sampled by the preceding Tx incarnation");
  link->finish();
}

static void rxReadSACK(bool fill)
{
  ZuTestScopeRT(rxReadSACK);
  App app;
  ZmRef<Link> link = new Link{&app};
  app.holdTx = true;
  bool tail = link->receive(input(app, "efgh", 4));
  auto waiting = Zutcp::TCPStateTest::header(*link);
  ZuCHECK(tail && !waiting.flags && app.txJobs.count_(),
    "a pending nonempty SACK report cannot be mixed with an earlier Tx snapshot");
  if (fill) {
    bool head = link->receive(input(app, "abcd"));
    auto common = Zutcp::TCPStateTest::header(*link);
    ZuCHECK(head && common.ack == 2009 && !common.options.sackCount,
      "gap closure clears SACK synchronously before old exceptional events execute");
  }
  app.holdTx = false; app.runTx();
  auto header = Zutcp::TCPStateTest::header(*link);
  ZuCHECK(header.ack == (fill ? 2009u : 2001u) &&
      (fill ? !header.options.sackCount : header.options.sackCount == 1 &&
        header.options.sack[0].left == 2005 && header.options.sack[0].right == 2009),
    "exceptional SACK arrival preserves exact edges and cannot revive a retired gap");
  link->finish();
}

static void finACKCovered(bool accepted)
{
  ZuTestScopeRT(finACKCovered);
  App app;
  ZmRef<Link> link = new Link{&app};
  app.holdTx = app.holdRx = true;
  bool received = link->receive(input(app, "end", 0, 0, 0x11));
  unsigned attempts = 0, sent = 0;
  bool early = link->output({}, [&attempts, &sent, accepted](auto, const Zxdp::CommitFn &commit) {
    ++attempts;
    if (!accepted || !commit()) return false;
    ++sent;
    return true;
  });
  app.holdTx = false; app.runTx();
  bool pending = Zutcp::TCPStateTest::pendingACK(*link);
  bool held = Zutcp::TCPStateTest::heldACK(*link);
  bool later = Zutcp::TCPStateTest::flush(*link, [&attempts, &sent](
      auto, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    ++attempts; ++sent; return true;
  });
  ZuCHECK(received && early == accepted && pending == !accepted && !held &&
      later == !accepted && sent == 1 && attempts == (accepted ? 1u : 2u),
    "first FIN uses an already accepted ACK, while refused output retains an urgent retry");
  // An unchanged frontier from a later duplicate is evidence of a lost ACK.
  bool duplicate = link->receive(input(app, {}, 3, 0, 0x11));
  pending = Zutcp::TCPStateTest::pendingACK(*link);
  bool repeated = Zutcp::TCPStateTest::flush(*link, [&attempts, &sent](
      auto, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    ++attempts; ++sent; return true;
  });
  ZuCHECK(duplicate && pending && repeated && sent == 2,
    "a retransmitted FIN forces another ACK despite accepted final-frontier coverage");
  link->finish();
}

static void finACKBeforeNotice()
{
  ZuTestScope(finACKBeforeNotice);
  App app;
  ZmRef<Link> link = new Link{&app};
  Zutcp::SentFrameRef local;
  local.kind = Zutcp::SentFrameKind::Data; local.fin = true;
  bool setup = Zutcp::TCPStateTest::record(*link, local);
  app.holdTx = app.holdRx = true;
  bool peer = link->receive(input(app, "end", 0, 1, 0x11));
  Zutcp::TCPHeader header;
  bool sent = link->output({}, [&header](
      auto buf, const Zxdp::CommitFn &commit) {
    if (!commit()) return false;
    header = buf->tcp;
    return true;
  });
  ZuCHECK(setup && peer && sent && header.ack == 2005 && !app.rxJobs.count_(),
    "existing Tx work can ACK the atomic final frontier before the queued peer-FIN event");
  app.holdTx = false; app.runTx();
  ZuCHECK(app.rxJobs.count_() == 1 && !link->finishes,
    "peer-FIN event detects the already accepted non-RST ACK and publishes completion once");
  unsigned n = consume(*link);
  bool retired = n == 3 && link->retireRx(n);
  app.holdRx = false; app.runRx(); app.runTx();
  ZuCHECK(retired && link->finishes == 1 && link->completions == 1,
    "quiet FIN drain completes without another packet or another ACK transmission");
  link->finish();
}

static void pathRxLocal()
{
  ZuTestScope(pathRxLocal);
  App app;
  ZmRef<Link> link = new Link{&app};
  app.holdTx = true;
  Zutcp::TCPStateTest::pathReceived(*link, {ZiIP{"198.18.0.2"}, 18192}, 17);
  Zutcp::TCPStateTest::pathReceived(*link, {ZiIP{"198.18.0.3"}, 18192}, 29);
  Zutcp::TCPStateTest::pathReceived(*link, {ZiIP{"198.18.0.2"}, 18192}, 11);
  ZuCHECK(!app.txJobs.count_() && Zutcp::TCPStateTest::pathBytes(*link) == 28,
    "Rx path telemetry counts only the current peer and schedules no Tx mirror update");
  link->finish();
}

static void txReadFresh()
{
  ZuTestScope(txReadFresh);
  App app;
  ZmRef<Link> link = new Link{&app, true};
  uint64_t next = 99, ack = 99, sent = 99;
  ZuCHECK(link->isServer() && !link->dataGen() &&
      Zutcp::TCPStateTest::frontiers(*link, next, ack, sent) &&
      !next && !ack && !sent,
    "fresh passive connection publishes its empty generation-zero Tx view");
  // Hold owner work so a broken replay path cannot recurse during this test.
  app.holdTx = true;
  bool received = link->receive(input(app, "abc"));
  ZuCHECK(received && !app.rxJobs.count_() &&
      link->reader().length() == 3,
    "generation-zero ACK-bearing DATA is admitted directly");
  Zutcp::TCPStateTest::reset(*link);
  ZuCHECK(link->dataGen() == 1 &&
      !Zutcp::TCPStateTest::frontiers(*link, next, ack, sent),
    "first Rx reset rejects generation-zero Tx evidence before its owner reset");
  app.holdTx = false; app.runTx();
  ZuCHECK(Zutcp::TCPStateTest::frontiers(*link, next, ack, sent) &&
      !next && !ack && !sent,
    "first Tx reset publishes the empty generation-one view");
  link->finish();
}

static void txReadFrontiers()
{
  ZuTestScope(txReadFrontiers);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  auto ref = retained(a, pool, 4);
  app.holdRx = true;
  bool recorded = Zutcp::TCPStateTest::record(*link, ref);
  uint64_t next = 0, ack = 0, sent = 0;
  bool visible = Zutcp::TCPStateTest::frontiers(*link, next, ack, sent);
  ZuCHECK(recorded && visible && next == 4 && !ack && !sent && !app.rxJobs.count_(),
    "accepted DATA is visible to Rx immediately without an owner mirror callback");
  Zutcp::TCPStateTest::ack(*link, 4);
  Zutcp::TCPStateTest::ack(*link, 4);
  visible = Zutcp::TCPStateTest::frontiers(*link, next, ack, sent);
  ZuCHECK(visible && next == 4 && ack == 4 && !app.rxJobs.count_() && a.returned == 1,
    "ACK retirement and duplicate ACK expose monotonic frontiers without Rx jobs");
  Zutcp::TCPStateTest::timestamps(*link, 100);
  bool premature = Zutcp::TCPStateTest::learnTimestamp(*link, 1, 101);
  Zutcp::TCPStateTest::advanceRx(*link, 4);
  bool refused = link->output({}, [](auto, const Zxdp::CommitFn &) { return false; });
  visible = Zutcp::TCPStateTest::frontiers(*link, next, ack, sent);
  bool withheld = visible && !sent && !Zutcp::TCPStateTest::learnTimestamp(*link, 1, 101);
  bool accepted = link->output({}, [](auto, const Zxdp::CommitFn &commit) { return commit(); });
  visible = Zutcp::TCPStateTest::frontiers(*link, next, ack, sent);
  bool learned = Zutcp::TCPStateTest::learnTimestamp(*link, 1, 101);
  ZuCHECK(!premature && !refused && withheld && accepted && visible && sent == 4 &&
      learned && Zutcp::TCPStateTest::recent(*link) == 101 && !app.rxJobs.count_(),
    "only accepted ACK publication permits TS.Recent advancement, with no ordinary Rx post");
  link->finish(); link = nullptr; ref = {};
  ZuCHECK(a.valid && a.returned == 1, "view publication preserves exact backing retirement");
}

static void txReadReset()
{
  ZuTestScope(txReadReset);
  App app;
  ZmRef<Link> link = new Link{&app};
  Native a{app.mx()}; Zxdp::BufPool pool{1, sizeof(a.bytes)};
  auto ref = retained(a, pool, 4);
  bool recorded = Zutcp::TCPStateTest::record(*link, ref);
  app.holdTx = true;
  Zutcp::TCPStateTest::reset(*link);
  uint64_t next = 99, ack = 99, sent = 99;
  ZuCHECK(recorded && !Zutcp::TCPStateTest::frontiers(*link, next, ack, sent),
    "Rx reset rejects the previous Tx generation before its owner reset runs");
  app.holdTx = false; app.runTx();
  ZuCHECK(Zutcp::TCPStateTest::frontiers(*link, next, ack, sent) &&
      !next && !ack && !sent,
    "completed Tx reset publishes one fresh empty generation");
  link->finish(); link = nullptr; ref = {};
  ZuCHECK(a.valid && a.returned == 1, "reset view retains no old physical backing");
}

static void txReadDSACK()
{
  ZuTestScope(txReadDSACK);
  App app;
  ZmRef<Link> link = new Link{&app};
  bool received = link->receive(input(app, "abcd"));
  app.holdRx = true;
  bool ordinary = link->output({}, [](auto, const Zxdp::CommitFn &commit) { return commit(); });
  ZuCHECK(received && ordinary && !app.rxJobs.count_(),
    "ordinary accepted cumulative ACK does not schedule SACK bookkeeping");
  bool duplicate = link->receive(input(app, "abcd"));
  // Drain ACK-intent work; only the accepted DSACK below is held.
  app.runRx();
  bool first = link->output({}, [](auto, const Zxdp::CommitFn &commit) { return commit(); });
  bool notified = app.rxJobs.count_() == 1;
  duplicate &= link->receive(input(app, "abcd"));
  if (auto job = app.rxJobs.shift()) job();
  auto options = Zutcp::TCPStateTest::rxOptions(*link);
  ZuCHECK(duplicate && first && notified && options.dsack(4),
    "old accepted DSACK event cannot clear a newer duplicate report");
  app.runRx();
  bool second = link->output({}, [](auto, const Zxdp::CommitFn &commit) { return commit(); });
  app.runRx();
  options = Zutcp::TCPStateTest::rxOptions(*link);
  ZuCHECK(second && !options.sackCount,
    "matching exceptional DSACK completion clears its report exactly once");
  link->finish();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(rxReadCommon);
  ZuTestCall(rxReadSACK, false);
  ZuTestCall(rxReadSACK, true);
  ZuTestCall(finACKBeforeNotice);
  ZuTestCall(finACKCovered, false);
  ZuTestCall(finACKCovered, true);
  ZuTestCall(pathRxLocal);
  ZuTestCall(txReadFresh);
  ZuTestCall(txReadFrontiers);
  ZuTestCall(txReadReset);
  ZuTestCall(txReadDSACK);
  ZuTestCall(peerEcho, 0);
  ZuTestCall(peerEcho, 1);
  ZuTestCall(peerEcho, 2);
  ZuTestCall(wholeOriginal, false, false);
  ZuTestCall(wholeOriginal, true, false);
  ZuTestCall(wholeOriginal, false, true);
  ZuTestCall(wholeOriginal, true, true);
  ZuTestCall(originalTxOrder, 0);
  ZuTestCall(originalTxOrder, 1);
  ZuTestCall(originalTxOrder, 2);
  ZuTestCall(originalTxOrder, 3);
  ZuTestCall(acceptedFrontierBurst);
  ZuTestCall(committedACKTimestamp);
  ZuTestCall(synCommitAdmission, 0);
  ZuTestCall(synCommitAdmission, 1);
  ZuTestCall(synCommitAdmission, 2);
  ZuTestCall(probeQuota, false);
  ZuTestCall(probeQuota, true);
  ZuTestCall(admissionAndACK);
  ZuTestCall(invalidACKInput, false);
  ZuTestCall(invalidACKInput, true);
  ZuTestCall(directACKAdmission, 0);
  ZuTestCall(directACKAdmission, 1);
  ZuTestCall(directACKAdmission, 2);
  ZuTestCall(directACKAdmission, 3);
  ZuTestCall(directACKAdmission, 4);
  ZuTestCall(ackHoldTimer, false);
  ZuTestCall(ackHoldTimer, true);
  ZuTestCall(ackHoldCoalesce);
  ZuTestCall(ackHoldByteBound);
  ZuTestCall(handshakeRTT, false, 0);
  ZuTestCall(handshakeRTT, false, 1);
  ZuTestCall(handshakeRTT, false, 2);
  ZuTestCall(handshakeRTT, false, 3);
  ZuTestCall(handshakeRTT, false, 4);
  ZuTestCall(handshakeRTT, false, 5);
  ZuTestCall(handshakeRTT, true, 0);
  ZuTestCall(handshakeRTT, true, 1);
  ZuTestCall(handshakeRTT, true, 2);
  ZuTestCall(handshakeRTT, true, 3);
  ZuTestCall(handshakeRTT, true, 4);
  ZuTestCall(handshakeRTT, true, 5);
  ZuTestCall(selectiveRecovery);
  ZuTestCall(cumulativeFallback);
  ZuTestCall(receiveSACK);
  ZuTestCall(emptySACK);
  ZuTestCall(emptyDSACK);
  ZuTestCall(paws, false);
  ZuTestCall(paws, true);
  ZuTestCall(pawsBoundary, 0);
  ZuTestCall(pawsBoundary, 1);
  ZuTestCall(pawsBoundary, 2);
  ZuTestCall(pawsBoundary, 3);
  ZuTestCall(pawsBoundary, 4);
  ZuTestCall(pawsBoundary, 5);
  ZuTestCall(pawsClock, 0);
  ZuTestCall(pawsClock, 1);
  ZuTestCall(pawsClock, 2);
  ZuTestCall(pawsClock, 3);
  ZuTestCall(pawsAdmission, 0);
  ZuTestCall(pawsAdmission, 1);
  ZuTestCall(pawsAdmission, 2);
  ZuTestCall(mssBudget, false, 0, false);
  ZuTestCall(mssBudget, true, 0, false);
  ZuTestCall(mssBudget, false, 1200, true);
  ZuTestCall(mssBudget, false, 37, true);
  ZuTestCall(mssBudget, false, 800, true);
  ZuTestCall(positiveWindow, 0);
  ZuTestCall(positiveWindow, 1);
  ZuTestCall(positiveWindow, 32);
  ZuTestCall(pacedECN);
  ZuTestCall(peerAckMerge, 0);
  ZuTestCall(peerAckMerge, 1);
  ZuTestCall(peerAckMerge, 2);
  ZuTestCall(peerAckMerge, 3);
  ZuTestCall(peerAckMerge, 4);
  ZuTestCall(peerAckWindow, 0);
  ZuTestCall(peerAckWindow, 1);
  ZuTestCall(peerAckWindow, 2);
  ZuTestCall(peerAckWrap);
  ZuTestCall(peerAckBatch);
  ZuTestCall(peerAckExceptions);
  ZuTestCall(noCCAdmission);
  ZuTestCall(noCCRecovery, false);
  ZuTestCall(noCCRecovery, true);
  ZuTestCall(earlyPolicy, 0);
  ZuTestCall(earlyPolicy, 1);
  ZuTestCall(earlyPolicy, 2);
  ZuTestCall(rtoQuota, 0);
  ZuTestCall(rtoQuota, 1);
  ZuTestCall(rtoQuota, 2);
  ZuTestCall(rtoQuota, 3);
  ZuTestCall(tlpChoice, 0);
  ZuTestCall(tlpChoice, 1);
  ZuTestCall(tlpChoice, 2);
  ZuTestCall(timestampRTT, 0);
  ZuTestCall(timestampRTT, 1);
  ZuTestCall(timestampRTT, 2);
  ZuTestCall(timestampRTT, 3);
  ZuTestCall(timestampRTT, 4);
  ZuTestCall(boundedSACK, false);
  ZuTestCall(boundedSACK, true);
  ZuTestCall(testMxRxStub, false, false);
  ZuTestCall(testMxRxStub, true, false);
  ZuTestCall(testMxRxStub, false, true);
  ZuTestCall(testMxRxDelivery, false);
  ZuTestCall(testMxRxDelivery, true);
  ZuTestCall(testMxRxRegistered);
  ZuTestCall(contiguousHeldBacking, false);
  ZuTestCall(contiguousHeldBacking, true);
  ZuTestCall(contiguousQueued, false);
  ZuTestCall(contiguousQueued, true);
  ZuTestCall(mappedTxQueue);
  ZuTestCall(oldIncarnationACK);
  ZuTestCall(sequenceCycle, false);
  ZuTestCall(sequenceCycle, true);
  ZuTestCall(receiveCE);
  ZuTestCall(originalCap, 0);
  ZuTestCall(originalCap, 1);
  ZuTestCall(originalCap, 2);
  ZuTestCall(originalCap, 3);
  ZuTestCall(resetQualification);
  ZuTestCall(fastRecovery, 0);
  ZuTestCall(fastRecovery, 1);
  ZuTestCall(fastRecovery, 2);
  ZuTestCall(fastRecovery, 3);
  ZuTestCall(fastRecovery, 4);
  ZuTestCall(fastRecovery, 5);
  ZuTestCall(fastRecovery, 6);
  ZuTestCall(fastRecovery, 7);
  ZuTestCall(readiness);
  ZuTestCall(finACKBeforeDequeue);
  ZuTestCall(finACK, false, false);
  ZuTestCall(finACK, true, false);
  ZuTestCall(finACK, false, true);
}
