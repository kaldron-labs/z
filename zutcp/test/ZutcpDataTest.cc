//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"

using namespace ZuTestUtil;

namespace Zutcp {
struct TCPStateTest {
  template <typename T> static auto &core(T &link) {
    return static_cast<typename T::Base &>(link);
  }
  template <typename T> static void stage(T &link, bool sack = true) {
    auto &c = core(link);
    c.resetLink();
    c.configLocalParams(link.app());
    c.initClientPathTx({ZiIP{"198.18.0.1"}, 18191}, {ZiIP{"198.18.0.2"}, 18192}, 1200);
    c.m_closedKeyRx = ClosedTupleKey{ZiSockAddr{ZiIP{"198.18.0.1"}, 18191},
      ZiSockAddr{ZiIP{"198.18.0.2"}, 18192}};
    c.m_txOrigins = c.m_rxOrigins = {1000, 2000, true, true, false};
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
  template <typename T> static bool queuedFIN(T &link) {
    auto &c = core(link);
    return c.m_rxState.complete() && c.m_rxDelivered < c.m_rxState.finalSize() &&
      c.rxByteNext() == c.m_rxState.finalSize() + 1;
  }
  template <typename T> static unsigned records(T &link) { return core(link).m_txPkts.count(); }
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
  template <typename T> static bool pendingInput(T &link) { return core(link).m_rxTFOPending; }
  template <typename T> static bool failedBudget(T &link) { return core(link).m_tcpBudgetFailed; }
  template <typename T> static void reset(T &link) { core(link).resetLink(); }
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
  template <typename T> static uint32_t recent(T &link) { return core(link).m_rxTSRecent; }
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
      c.sentTCPTx(header, false);
    }
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
    return c.m_tlpTimerSelected && c.m_ptoTimer && !c.m_rtoTimer;
  }
  template <typename T> static bool rtoSelected(T &link) {
    auto &c = core(link);
    return !c.m_tlpTimerSelected && !c.m_ptoTimer && c.m_rtoTimer &&
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
    return c.fastRecoveryReady() && !c.congestionAllowance() &&
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

  App() {
    rx = ZutcpTest::pktRxAllocator(&multiplex);
    control = multiplex.allocator<Zi::IOBufType::TxTCP, Zutcp::PacketBuf, Zutcp::BufSize>();
    ZmAssert_(Base::init(Zutcp::HubParams{&multiplex}.maxData(4096).timestamps(false)));
    ZmAssert_(multiplex.start());
  }
  ~App() { Base::final(); multiplex.stop(); }
  bool nativeTx() const { return true; }
  bool ioActive() const { return false; } // Controlled owners have no raw Stack/endpoint.
  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
  ZuTime transportNow() const { return clock; }
  Zi::IOBufAllocator *dataTxAllocator() const { return Zutcp::inlineAllocator(); }
  bool txWritable(const Zutcp::TxBacking &) const { return writable; }
  Zxdp::BufPool *txPool(Zi::IOBufAllocator *a) const {
    return a && (a == nativeAllocator || a == nativeAllocator2) ? nativePool : nullptr;
  }
  bool retainRx(ZiIOBuf *, ZmRef<Zutcp::RxAccount> &, uint64_t) { ++demands; return allowRx; }
  template <typename F> void txRun(F f) {
    if (holdTx) txJobs.push([f = ZuMv(f)]() mutable { f(); }); else f();
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
  Link(App *app) : Base{app} { Zutcp::TCPStateTest::stage(*this); }
  int process(Zutcp::Reader &) { ++processed; return 0; }
  bool applicationReadyRx() const { return ready; }
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
      }, [send = ZuMv(send)](auto buf, auto) mutable { return send(ZuMv(buf)); });
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
  bool sent = link->output(ref, [&a, &attempts, &geometry, accept, syn](auto buf) {
    ++attempts;
    geometry &= buf->data() == a.bytes + 256 && buf->length == 64 &&
      bool(buf->tcp.flags & 0x02) == syn && buf->retained;
    if (!accept) return false;
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
    first = link->output(ref, [link, &ref, &attempts, &reentry, accept](auto buf) {
      ++attempts;
      reentry = link->output(ref, [](auto) { return true; });
      if (accept) { buf->data__ = 0; buf->size = buf->skip = buf->length = 0; }
      return accept;
    });
    if (accept) second = link->output(ref, [](auto) { return true; });
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
  ZuCHECK(first && duplicate && Zutcp::TCPStateTest::pendingInput(*link) &&
      app.demands == 1 && app.txJobs.count_() == 3 &&
      !link->processed && !link->rxAdmitted(),
    "uncertain ACK retains one bounded candidate and delivers no bytes before Tx validation");
  if (stale) Zutcp::TCPStateTest::reset(*link);
  app.holdTx = false; app.runTx(); app.holdRx = false; app.runRx(); app.runTx();
  ZuCHECK(!link->processed && !link->rxAdmitted() && !link->disconnecting() &&
      !Zutcp::TCPStateTest::pendingInput(*link),
    "future ACK and replaced-generation input cannot deliver payload or corrupt the live owner");
  link->finish();
}

// Exercise the real accepted SYN publication and bounded ACK owner handoff.
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
  auto send = [&attempts, &wire](auto buf) {
    ++attempts; wire = buf->tcp; return true;
  };
  bool firstRefused = true;
  if (scenario == 1) {
    firstRefused = !link->output({}, [&attempts](auto) { ++attempts; return false; }, true);
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
    invalid = link->receive(ZuMv(d)) && !Zutcp::TCPStateTest::confirmed(*link) &&
      !*Zutcp::TCPStateTest::handshakeRTT(*link);
  }
  app.holdTx = app.holdRx = scenario == 5;
  auto d = input(app, {}, 0, 0, server ? 0x10 : 0x12);
  if (!server) d.tcp.seq = 2000;
  bool received = link->receive(ZuMv(d));
  bool delayed = scenario != 5 ||
    (Zutcp::TCPStateTest::pendingInput(*link) && !Zutcp::TCPStateTest::confirmed(*link));
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
  bool sent = link->output(ref, [&attempts](auto buf) {
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
    bool blocked = !link->output(ref, [&attempts](auto) { ++attempts; return true; });
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
  auto accept = [&attempts](auto buf) {
    ++attempts;
    buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
    return true;
  };
  bool sent = link->output(first, accept);
  uint64_t deadline = Zutcp::TCPStateTest::paced(*link);
  bool blocked = !link->output(second, accept);
  bool ack = link->output({}, [&attempts](auto) { ++attempts; return true; });
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
  bool refused = !link->output(second, [](auto) { return false; });
  bool retained = Zutcp::TCPStateTest::cwr(*link);
  Zutcp::TCPHeader header;
  bool accepted = link->output(second, [&attempts, &header](auto buf) {
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
  bool normal = link->output(refs[0], [&attempts](auto) { ++attempts; return false; });
  bool first = false, reentry = true, second = true, other = true;
  Zutcp::TCPStateTest::rtoAttempt(*link, [
    link, &refs, &attempts, &first, &reentry, &second, &other, scenario] {
    other = link->output(refs[1], [&attempts](auto) { ++attempts; return false; });
    first = link->output(refs[0], [link, &refs, &attempts, &reentry, scenario](auto buf) {
      ++attempts;
      reentry = link->output(refs[0], [&attempts](auto) { ++attempts; return false; });
      if (scenario == 0) return false;
      buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
      if (scenario == 2) Zutcp::TCPStateTest::reset(*link);
      return true;
    });
    if (scenario != 0) second = link->output(refs[0], [&attempts](auto) {
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
    "bounded partial SACK yields one continuation and restores physical RTO before its final "
    "batch");
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
      !Zutcp::TCPStateTest::pendingInput(*link) && app.demands == 1 &&
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
  bool ack1 = link->output({}, [&firstACK](auto buf) { firstACK = buf->tcp; return true; });
  bool ack2 = link->output({}, [&secondACK](auto buf) { secondACK = buf->tcp; return true; });
  bool cwr = link->receive(input(app, {}, 3, 0, 0x90));
  bool ack3 = link->output({}, [&clearedACK](auto buf) { clearedACK = buf->tcp; return true; });
  auto renewed = input(app, "two", 3, 0, 0x90); renewed.ecn = Zutcp::EcnMark::CE;
  bool both = link->receive(ZuMv(renewed));
  bool ack4 = link->output({}, [&renewedACK](auto buf) { renewedACK = buf->tcp; return true; });
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
  app.holdTx = false; app.runTx(); app.runRx(); app.runTx();
  ZuCHECK(setup && failed && closed && pending && repeated &&
      Zutcp::TCPStateTest::failures(*link) == 1 &&
      (kind != 3 || (!link->txQueueCount() && a.returned == 1)),
    "cap invalidation protects published, queued, retained and pending originals and fails once");
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
  bool ordinary = link->output(candidate, [&attempts](auto) { ++attempts; return false; });
  bool newAllowed = link->recoveryOutput(newData,
    [&attempts](auto) { ++attempts; return false; });
  bool reentry = true, direct = true, accepted = false;
  auto accept = [native = link.ptr(), &candidate, &attempts, &reentry, &direct, scenario](
      auto buf) {
    ++attempts;
    reentry = native->recoveryOutput(candidate,
      [&attempts](auto) { ++attempts; return false; });
    direct = native->output(candidate, [&attempts](auto) { ++attempts; return false; });
    buf->data__ = 0; buf->size = buf->skip = buf->length = 0;
    if (scenario == 3) Zutcp::TCPStateTest::reset(*native);
    return true;
  };
  bool refused = true, retainedPermission = true;
  if (scenario == 2 || scenario == 7) {
    if (scenario == 2) Zutcp::TCPStateTest::delayRecovery(*link);
    else Zutcp::TCPStateTest::window(*link, 0);
    refused = !link->recoveryOutput(candidate,
      [&attempts](auto) { ++attempts; return false; });
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
	[&attempts](auto) { ++attempts; return false; });
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
  bool accepted = link->output({}, [&attempts](auto) { ++attempts; return true; });
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
  bool refused = link->output({}, [&attempts](auto) { ++attempts; return false; });
  ZuCHECK(setup && peer && !refused && !link->finishes && !link->completions,
    "refused final ACK cannot finish native retirement or queued disconnect callbacks");
  app.holdRx = stale;
  bool accepted = link->output({}, [&attempts](auto) { ++attempts; return true; });
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

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(wholeOriginal, false, false);
  ZuTestCall(wholeOriginal, true, false);
  ZuTestCall(wholeOriginal, false, true);
  ZuTestCall(wholeOriginal, true, true);
  ZuTestCall(probeQuota, false);
  ZuTestCall(probeQuota, true);
  ZuTestCall(admissionAndACK);
  ZuTestCall(invalidACKInput, false);
  ZuTestCall(invalidACKInput, true);
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
  ZuTestCall(paws, false);
  ZuTestCall(paws, true);
  ZuTestCall(mssBudget, false, 0, false);
  ZuTestCall(mssBudget, true, 0, false);
  ZuTestCall(mssBudget, false, 1200, true);
  ZuTestCall(mssBudget, false, 37, true);
  ZuTestCall(mssBudget, false, 800, true);
  ZuTestCall(positiveWindow, 0);
  ZuTestCall(positiveWindow, 1);
  ZuTestCall(positiveWindow, 32);
  ZuTestCall(pacedECN);
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
