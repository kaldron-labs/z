//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZiFile.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

#define ZUTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

using TxBufAlloc = Zutcp::TxBufAlloc<>;

static ZuBSpan span_(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
}

#ifdef Zutcp_DEBUG
static Zi::Path g_residue;

static void closeQLog(ZutcpLogger::Trace &trace)
{
  ZmBlock<>{}([&trace](auto wake) {
    ZutcpLogger::close(trace, ZuMv(wake));
  });
}
#endif

struct App : public Zutcp::Hub<App> {
  using Base = Zutcp::Hub<App>;
  ZmQueue<ZmFn<>> pendingTx;
  bool producer = false, holdTxInvoke = false;
  Zi::IOBufAllocator *dataTxAllocator() const { return Zutcp::inlineAllocator(); }
  bool txWritable(const Zutcp::TxBacking &) const { return rawWritable; }
  bool nativeTx() const { return requireNativeTx; }
  ZuTime transportNow() const { return *testNow ? testNow : Base::transportNow(); }

  App(unsigned maxPayload = Zutcp::MinPktPayload, uint64_t rxWindow = 0,
      ZuTime initialRto = {}) :
    m_mx{mxParams_()} {
    auto params = Zutcp::HubParams(&m_mx);
    params.maxPayload(maxPayload);
    if (rxWindow) params.maxData(rxWindow);
    if (*initialRto) params.initialRto(initialRto);
    bool ok = Base::init(params);
    ZiAssert(ok, "Zutcp", (), "connection test app init failed", return);
    ZiAssert(m_mx.start(), "Zutcp", (),
      "connection test multiplexer start failed", return);
  }
  ~App() {
    Base::final();
    m_mx.stop();
  }

  bool rxInvoked() const { return true; }
  bool txInvoked() const { return !producer; }
  template <typename L> void rxRun(L l) { l(); }
  template <typename L> void rxInvoke(L l) { l(); }
  template <typename L> void txRun(L l) {
    if (deferredTx) m_mx.run(ZuMv(l), Base::txThread());
    else l();
  }
  template <typename L> void txInvoke(L l) {
    if (holdTxInvoke) pendingTx.push([l = ZuMv(l)]() mutable { (void)l(); });
    else (void)l();
  }
  template <typename O, typename L> void txInvoke(O *, L l) { txInvoke(ZuMv(l)); }
  void runPendingTx() {
    producer = false;
    while (auto fn = pendingTx.shift()) fn();
  }
  void drainTx() {
    ZmBlock<>{}([this](auto wake) {
	m_mx.invoke([wake = ZuMv(wake)]() mutable { wake(); },
	  Base::txThread());
    });
  }
  template <typename L> void onTx(L l) {
    ZmBlock<>{}([this, l = ZuMv(l)](auto wake) mutable {
      m_mx.run([l = ZuMv(l), wake = ZuMv(wake)]() mutable {
        l();
        wake();
      }, Base::txThread());
    });
  }

  bool		deferredTx = false;
  bool		rawWritable = true;
  bool		requireNativeTx = false;
  ZuTime		testNow;

private:
  static ZiMxParams mxParams_() {
    return ZiMxParams()
      .scheduler([](auto &s) {
	s.nThreads(4);
      })
      .rxThread(1).txThread(2);
  }

  ZiMultiplex	m_mx;
};
struct TestLink;
struct TestCIDRoutes {
  bool add(const Zutcp::CxnID &, TestLink *) { ++adds; return true; }
  bool retire(const Zutcp::CxnID &) {
    ++retires;
    return true;
  }
  bool tombstone(const Zutcp::CxnID &) {
    ++tombstones;
    return true;
  }

  unsigned	adds = 0;
  unsigned	retires = 0;
  unsigned	tombstones = 0;
};
// The real ACK/loss/controller path calls these CRTP producer hooks on owner4.
// Recovery notification really posts to that owner; ordinary bulk output can
// consume every byte of newly opened congestion credit before it executes.
struct RecoveryService {
  enum { MaxEvents = 16 };
  unsigned events = 0, recovered = 0, blocked = 0, queued = 0;
  char order[MaxEvents] = {};
  uint64_t recoveredPN = 0;
  ZiIOBuf *origin = nullptr;
  bool valid = true;

  void note(char kind) {
    if (events < MaxEvents) order[events++] = kind;
    else valid = false;
  }
};

struct TestLink :
  public ZmNHeap<TestLink>,
  public Zutcp::Connection<App, TestLink,
    TxBufAlloc>
{
friend inline ZuStringT<"Zutcp.DataTest.Link"> ZmNHeapID(TestLink *);

  using Base = Zutcp::Connection<App, TestLink,
    TxBufAlloc>;
  friend inline ZuStringT<"Zutcp.DataTest.Link"> ZmNHeapID(TestLink *);
  using ZmNHeap<TestLink>::operator new;
  using ZmNHeap<TestLink>::operator delete;

  using Base::txNext;
  using Base::txAckd;
  using Base::acceptTxData;
  using Base::peerParams;
  using Base::ackDelay_;
  using Base::lossThreshold;

  RecoveryService *service = nullptr;

  int process(Zutcp::Reader &) { ++processed; return 0; }
  void txDequeued_() { if (dequeued) dequeued(); }
  TestLink(App *app, bool isServer = false) : Base{app, isServer} {
    Base::configLocalParams(app);
  }
  bool resetOriginsForTest(Zutcp::TCPOrigins origins) {
    Base::resetLink_();
    Base::configLocalParams(app());
    return Base::setTCPOriginsRx_(origins);
  }
  void resetRuntimeForTest() {
    bool ok = resetOriginsForTest({UINT32_MAX, UINT32_MAX, true, true, Base::isServer()});
    ZiAssert(ok, "Zutcp", (), "fixture origins must initialize", return);
  }
  Zutcp::TCPHeader rangeTCPForTest(const Zutcp::SentFrameRef &ref) const {
    Base::TxPktRefs refs;
    refs.add(ref);
    return Base::tcpHeader(&refs);
  }
  bool persistWireForTest(Zutcp::TCPHeader &header) {
    Zutcp::PktBuild build;
    int n = Zutcp::FrameCodec::writePing(build.scratch(), build.scratchAvail());
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    Base::TxPktRefs refs;
    return Base::sendShortPkt_(CIDSel::Peer, 2, build, {}, {}, &refs, false,
      []() -> ZmRef<Zutcp::PacketBuf> {
	static ZiMultiplex mx;
	auto a = mx.allocator<Zi::IOBufType::TxTCP, Zutcp::PacketBuf, Zutcp::BufSize>();
	auto buf = a->allocBuf();
	return ZmRef<Zutcp::PacketBuf>{static_cast<Zutcp::PacketBuf *>(buf.ptr())};
      },
      [&header](auto buf, ZiSockAddr, Zutcp::EcnMark::T) { header = buf->tcp; return true; });
  }
  void closeForTest(uint64_t errorCode = 0) {
    Base::closeState(errorCode);
    teardown();
  }


  bool queueDataBlocked(uint64_t value) {
    return Base::queueBlocked(value);
  }
  bool queueMaxData(uint64_t value) {
    return Base::txQueueControl_(Zutcp::ControlFrame::flowUpdate(
      Zutcp::FlowUpdate{
	Zutcp::FrameType::MaxData, value}));
  }
  void scheduleDataForTest() { Base::dataWritable(); }
  bool dataReadyForTest() const { return Base::txReady(); }
  void grantDataCredit(uint64_t value) { Base::txApplyMaxData_(value); }
  bool flushControlSendFails() {
    return Base::flushData(
      ZiSockAddr{},
      [](Zutcp::PktBuild &) { return true; },
      [](Zutcp::PktBuild &, ZiSockAddr, const Base::TxPktRefs &) {
	return false;
      });
  }
  bool flushControlSends() {
    return Base::flushData(
      ZiSockAddr{},
      [](Zutcp::PktBuild &) { return true; },
      [](Zutcp::PktBuild &, ZiSockAddr, const Base::TxPktRefs &) {
	return true;
      });
  }
  bool flushDataBatch() {
    return Base::flushDataBatch_(
      ZiSockAddr{},
      [](Zutcp::PktBuild &) { return true; },
      [this](Zutcp::PktBuild &build, ZiSockAddr, const Base::TxPktRefs &refs) {
	bool ok = Base::recordPktTx(
	  Zutcp::PktNumSpace::AppData, sentPkts, build.bytes(), {}, &refs, true);
	if (ok) ++sentPkts;
	return ok;
      });
  }
  unsigned flushFrameRefs(
    Zutcp::SentFrameKind::T *kinds, uint64_t *offsets,
    unsigned capacity) {
    unsigned refs = 0;
    bool ok = Base::flushData(
      ZiSockAddr{},
      [](Zutcp::PktBuild &) { return true; },
      [&refs, &kinds, &offsets, capacity](
	  Zutcp::PktBuild &, ZiSockAddr, const Base::TxPktRefs &r) {
	refs = r.count();
	for (unsigned i = 0; i < refs && i < capacity; ++i) {
	  kinds[i] = r[i].kind;
	  offsets[i] = r[i].offset;
	}
	return true;
      });
    return ok ? refs : 0;
  }
  unsigned flushSentRefs(Zutcp::SentFrameRef *refs, unsigned capacity) {
    unsigned n = 0;
    bool ok = Base::flushData(
      ZiSockAddr{},
      [](Zutcp::PktBuild &) { return true; },
      [&n, refs, capacity](
	  Zutcp::PktBuild &, ZiSockAddr, const Base::TxPktRefs &r) {
	n = r.count();
	for (unsigned i = 0; i < n && i < capacity; ++i) refs[i] = r[i];
	return true;
      });
    return ok ? n : 0;
  }
  void recordSentPkt(
    Zutcp::PktNumSpace::T level, uint64_t pn, unsigned bytes,
    const Zutcp::SentFrameRef &ref, bool ackEliciting,
    Zutcp::PktType::T packetType = Zutcp::PktType::N,
    Zutcp::EcnMark::T ecn = Zutcp::EcnMark::NotECT) {
    Base::recordTxPkt(
      level, pn, bytes, ref, ackEliciting, false, 0,
      Zutcp::PktNumSpace::N, 0, packetType, ecn);
    if (level == Zutcp::PktNumSpace::AppData) Base::acceptTxData(ref);
    Base::setTxPN(level, pn + 1);
  }
  void recordSentPkt(
    Zutcp::PktNumSpace::T level, uint64_t pn, unsigned bytes,
    const Base::TxPktRefs &refs, bool ackEliciting,
    Zutcp::PktType::T packetType = Zutcp::PktType::N) {
    Base::recordTxPkt(
      level, pn, bytes, refs, ackEliciting, false, 0,
      Zutcp::PktNumSpace::N, 0, packetType);
    if (level == Zutcp::PktNumSpace::AppData) Base::acceptTxData(refs);
    Base::setTxPN(level, pn + 1);
  }
  unsigned flushRecordedRefs(Zutcp::SentFrameRef *refs, unsigned capacity) {
    unsigned n = 0;
    bool ok = Base::flushData(
      ZiSockAddr{},
      [](Zutcp::PktBuild &) { return true; },
      [this, &n, refs, capacity](Zutcp::PktBuild &build, ZiSockAddr,
	  const Base::TxPktRefs &r) {
	n = r.count();
	for (unsigned i = 0; i < n && i < capacity; ++i) refs[i] = r[i];
	recordSentPkt(
	  Zutcp::PktNumSpace::AppData, sentPkts, build.bytes(), r, true);
	++sentPkts;
	return true;
      });
    return ok ? n : 0;
  }
  bool rebuildControl(
    const Zutcp::SentFrameRef &ref, Zutcp::Frame &frame) {
    Zutcp::PktBuild build;
    if (!Base::buildRetxCtl(build, ref) || !build.count())
      return false;
    unsigned used = 0;
    bool parsed = !Zutcp::FrameCodec::parse(
      ZuBSpan{build.data()[0].data(), unsigned(build.data()[0].length())},
      frame, used) && used == build.data()[0].length();
    return parsed;
  }
  bool flushCongestedData(Zutcp::SentFrameRef *out = nullptr) {
    return Base::sendDataPkt(
      ZiSockAddr{},
      [](Zutcp::PktBuild &) { return true; },
      [this, out](
	  Zutcp::PktBuild &build, ZiSockAddr,
	  const Base::TxPktRefs &refs) {
	if (out && refs.count()) *out = refs[0];
	recordSentPkt(
	  Zutcp::PktNumSpace::AppData, sentPkts, build.bytes(), refs, true);
	++sentPkts;
	return true;
      });
  }
  void fillCwnd() {
    Zutcp::LinkDiag diag = Base::diag_();
    while (diag.tx.bytesInFlight < diag.tx.congestionWindow) {
      uint64_t remaining =
	diag.tx.congestionWindow - diag.tx.bytesInFlight;
      unsigned bytes = remaining > app()->maxPayload() ?
	app()->maxPayload() : unsigned(remaining);
      recordSentPkt(
	Zutcp::PktNumSpace::AppData, sentPkts, bytes,
	Zutcp::SentFrameRef::control(), true);
      ++sentPkts;
      diag = Base::diag_();
    }
  }
  void reclaimData(uint64_t next = 0) {
    Zutcp::PktAckBatch batch;
    while (!Base::reclaimDataRtoBatch(next, batch)) { }
  }
  void ackBytes(uint64_t next, uint64_t largestPN, uint64_t delay = 0) {
    Base::AckSnapshot ack;
    ack.level = Zutcp::PktNumSpace::AppData;
    ack.byteNext = next;
    ack.delay = delay;
    new (ack.ranges.push()) Zutcp::AckRange{largestPN, largestPN};
    Base::processAckFrameTx_(ack);
  }
  void ackThrough(uint64_t pn) {
    Base::AckSnapshot ack;
    ack.level = Zutcp::PktNumSpace::AppData;
    new (ack.ranges.push()) Zutcp::AckRange{pn, 0};
    Base::processAckFrameTx_(ack);
  }
  void ackRange(uint64_t first, uint64_t largest) {
    Base::AckSnapshot ack;
    ack.level = Zutcp::PktNumSpace::AppData;
    new (ack.ranges.push()) Zutcp::AckRange{largest, first};
    Base::processAckFrameTx_(ack);
  }
  void ackOnly(
    uint64_t pn, Zutcp::PktNumSpace::T level = Zutcp::PktNumSpace::AppData) {
    Base::AckSnapshot ack;
    ack.level = level;
    new (ack.ranges.push()) Zutcp::AckRange{pn, pn};
    Base::processAckFrameTx_(ack);
  }
	  bool nextRetransmitRef(
	    Zutcp::PktNumSpace::T &level, Zutcp::SentFrameRef &ref,
	    Zutcp::PktNumSpace::T onlySpace = Zutcp::PktNumSpace::N) {
	    return Base::nextRetransmit_(level, ref, onlySpace);
	  }
	  bool forcePTOReclaimQLog(
	    Zutcp::PktNumSpace::T &level, unsigned &probes) {
	    Base::notePTOExpired();
	    return Base::reclaimPTO(level, probes);
	  }
	  void forcePTOProbeQLog(Zutcp::PktNumSpace::T level, unsigned probes) {
	    Base::notePTOProbe(level, probes);
	  }




  void sendControlRef(uint64_t pn, const Zutcp::SentFrameRef &ref) {
    recordSentPkt(
      Zutcp::PktNumSpace::AppData, pn, 100, ref, true);
  }

  void teardown() {
    ZmBlock<>{}([this](auto wake) {
      Base::disconnect_(false, ZuMv(wake));
    });
    app()->drainTx();
  }
  Zutcp::SentFrameRef prepareDataRef(uint64_t pn, uint64_t offset, unsigned length) {
    Zutcp::TxRange range;
    range.dataOffset = offset;
    range.length = length;
    auto ref = Zutcp::SentFrameRef::data(range, false);
    Base::recordTxPkt(Zutcp::PktNumSpace::AppData, pn, length, ref, true);
    Base::setTxPN(Zutcp::PktNumSpace::AppData, pn + 1);
    return ref;
  }
  void sendDataRef(uint64_t pn, uint64_t offset, uint64_t length, bool fin = false) {
    Zutcp::TxRange range{nullptr, 0, uint32_t(length), offset};
    Zutcp::SentFrameRef ref =
      Zutcp::SentFrameRef::data( range, fin);
    if (!length) {
      ref.offset = offset;
      ref.length = 0;
    }
    recordSentPkt(
      Zutcp::PktNumSpace::AppData, pn, 100, ref, true);
  }
  void advancePN(unsigned n) {
    for (unsigned i = 0; i < n; ++i)
      Base::recordPktTx(
	Zutcp::PktNumSpace::AppData, i, 1, {}, nullptr, false);
  }
#ifdef Zutcp_DEBUG
  void forcePN(uint64_t pn) {
    Base::setTxPN(Zutcp::PktNumSpace::AppData, pn);
  }
  void installLocalCIDRoutes(TestCIDRoutes &routes) {
    Base::installLocalCIDRoutes_(routes);
  }

  void tombstoneLocalCIDRoutes(TestCIDRoutes &routes) {
    Base::tombstoneLocalCIDRoutes_(routes);
  }
#endif
  unsigned pnLength() const {
    return Base::txPNLength(Zutcp::PktNumSpace::AppData);
  }

  void sendAckEliciting(uint64_t pn, unsigned bytes = 1200) {
    recordSentPkt(
      Zutcp::PktNumSpace::AppData, pn, bytes,
      Zutcp::SentFrameRef::control(), true);
  }
  void sendECN(
    uint64_t pn, Zutcp::EcnMark::T ecn, unsigned bytes = 1200) {
    recordSentPkt(
      Zutcp::PktNumSpace::AppData, pn, bytes,
      Zutcp::SentFrameRef::control(), true, Zutcp::PktType::N, ecn);
  }
#ifdef Zutcp_DEBUG
  void ackECN(uint64_t largest, uint64_t ect0, uint64_t ect1, uint64_t ce) {
    Base::setTxPN(Zutcp::PktNumSpace::AppData, largest + 1);
    Base::AckSnapshot ack;
    ack.level = Zutcp::PktNumSpace::AppData;
    new (ack.ranges.push()) Zutcp::AckRange{largest, 0};
    ack.ecn.ect0 = ect0;
    ack.ecn.ect1 = ect1;
    ack.ecn.ce = ce;
    Base::processAckFrameTx_(ack);
  }
  void ackNoECN(uint64_t largest) {
    Base::setTxPN(Zutcp::PktNumSpace::AppData, largest + 1);
    Base::AckSnapshot ack;
    ack.level = Zutcp::PktNumSpace::AppData;
    new (ack.ranges.push()) Zutcp::AckRange{largest, 0};
    Base::processAckFrameTx_(ack);
  }
#endif
  bool receiveMarked(
    uint64_t pn, Zutcp::EcnMark::T ecn, bool ackEliciting = true,
    Zutcp::PktNumSpace::T level = Zutcp::PktNumSpace::AppData) {
    if (Base::rxPktSeen(level, pn)) return false;
    Base::noteAck(level, pn, ackEliciting, ZiSockAddr{}, false, ecn);
    return true;
  }
  bool writePendingAck(Zutcp::PktBuild &build) {
    return Base::appendPendingAck(Zutcp::PktNumSpace::AppData, build);
  }
  Zutcp::PktBudget sendBudget() const { return Base::sendBudget_(); }
  void initServerPath() {
    Base::initServerPathTx_(ZiSockAddr{}, ZiSockAddr{});
  }
  void initServerPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initServerPathTx_(ZuMv(local), ZuMv(remote));
  }
#ifdef Zutcp_DEBUG




  void recoverPTO() {
    Base::ptoRecovery(
      []() { return false; },
      [](Zutcp::PktNumSpace::T) { return false; });
  }


#endif



  void pathReceived(unsigned bytes) { Base::recordPathRxTx_(ZiSockAddr{}, bytes); }
  bool pathSend(unsigned bytes) {
    ZmRef<ZiIOBuf> buf = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return ZutcpTest::pktTxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
    if (ZuUnlikely(!buf)) return false;
    buf->skip = 0;
    buf->length = bytes;
    return Base::sendPathPkt(
      ZuMv(buf), ZiSockAddr{},
      [](ZmRef<ZiIOBuf>, ZiSockAddr, Zutcp::EcnMark::T) { return true; });
  }
  void validatePath() { Base::validatePathTx_(); }
  void sparseAck() {
    constexpr unsigned Ranges = 16;
    Base::AckSnapshot ack;
    ack.level = Zutcp::PktNumSpace::AppData;
    ack.due = true;
    ack.largestRxTime = Base::nowUS();
    for (unsigned i = 0; i < Ranges; ++i) {
      uint64_t pn = uint64_t(i + 1) << 20;
      new (ack.ranges.push()) Zutcp::AckRange{pn, pn};
    }
    Base::noteAckTx(ack);
  }
  bool ackPending() const { return Base::pendingAck(Zutcp::PktNumSpace::AppData); }
  void wireTCP(const Zutcp::TCPHeader &header) { Base::applyTCP_(header); }
  void deliverRTO() { Base::cxnTimer(Zutcp::CxnTimer::RTO); }
  void packetAck(uint64_t frontier, uint64_t pn) {
    Zutcp::Frame frame;
    frame.type = Zutcp::FrameType::Ack;
    frame.byteNext = frontier;
    frame.ackRanges.push(Zutcp::AckRange{pn, pn});
    Base::processAckFrame(Zutcp::PktNumSpace::AppData, frame);
  }
  Zutcp::TCPHeader localTCP(uint64_t serial, uint64_t next, uint64_t limit) {
    Base::localWindow_(serial, next, limit);
    return Base::tcpHeader();
  }
  void deliverPersist() { Base::cxnTimer(Zutcp::CxnTimer::Persist); }
  bool sendPersistProbe(Zutcp::PktBuild &build) {
    Zutcp::Frame frame;
    unsigned used = 0;
    probeValid &= build.count() == 1 &&
      !Zutcp::FrameCodec::parse(build.data()[0], frame, used) &&
      frame.type == (app()->nativeTx() ? Zutcp::FrameType::Ping :
	Zutcp::FrameType::DataBlocked) && used == build.bytes() &&
      (app()->nativeTx() || frame.value == Base::txCreditLimit());
    Base::TxPktRefs refs;
    return Base::sendShortPkt_(CIDSel::Peer, 2, build, ZiSockAddr{}, {}, &refs, false,
      []() { return Zutcp::inlineAllocator()->allocBuf(); },
      [this](auto, ZiSockAddr, Zutcp::EcnMark::T) { ++probes; return probeAllowed; });
  }
  bool rawRetx(Zutcp::SentFrameRef &ref, unsigned &allocations, unsigned &sent,
      uint8_t *payload) {
    Zutcp::PktBuild build;
    if (!Base::buildRetxData(build, ref)) return false;
    Base::TxPktRefs refs;
    if (!refs.add(ref)) return false;
    return Base::sendShortPkt_(CIDSel::Peer,
      Base::txPNLength(Zutcp::PktNumSpace::AppData), build, ZiSockAddr{}, {}, &refs, true,
      [&allocations]() -> ZmRef<Zutcp::PacketBuf> { ++allocations; return nullptr; },
      [&sent, payload, &ref](auto buf, ZiSockAddr, Zutcp::EcnMark::T) {
	if (buf->data() + buf->length - ref.length != payload) return false;
	++sent;
	buf->data__ = 0;
	buf->size = buf->skip = buf->length = 0;
	return true;
      });
  }
  bool requeueData(const Zutcp::SentFrameRef &ref) {
    return Base::requeueDataRef(Zutcp::PktNumSpace::AppData, ref);
  }
  bool dataRetx(
    Zutcp::SentFrameRef &ref, ZmRef<ZiIOBuf> &wire,
    bool fail, bool &withAck) {
    Zutcp::PktBuild build;
    if (!Base::buildRetxData(build, ref)) return false;
    withAck = build.ack(Zutcp::PktNumSpace::AppData);
    uint8_t plain[Zutcp::BufSize];
    unsigned bytes = 0;
    bool retained = !ref.length;
    for (unsigned i = 0; i < build.count(); ++i) {
      const auto &part = build.data()[i];
      if (part.length() > sizeof(plain) - bytes) return false;
      memcpy(plain + bytes, part.data(), part.length());
      bytes += unsigned(part.length());
      if (ref.range.storage && part.data() == ref.range.storage->data + ref.range.offset &&
	  part.length() == ref.length) retained = true;
    }
    bool framed = false;
    for (unsigned offset = 0; offset < bytes;) {
      Zutcp::Frame frame;
      unsigned used = 0;
      if (Zutcp::FrameCodec::parse(span_(plain + offset, bytes - offset), frame, used) ||
	  !used) return false;
      offset += used;
      if (frame.type != Zutcp::FrameType::Data) continue;
      if (frame.offset != ref.offset ||
	  frame.length != ref.length || frame.fin != ref.fin ||
	  frame.payload != span_(ref.range.storage->data + ref.range.offset,
	    unsigned(ref.length))) return false;
      framed = true;
    }
    if (!framed || !retained) return false;
    Base::TxPktRefs refs;
    if (!refs.add(ref)) return false;
    return Base::sendShortPkt_(
      CIDSel::Peer, Base::txPNLength(Zutcp::PktNumSpace::AppData),
      build, ZiSockAddr{}, {}, &refs, true,
      []() {
	static Zi::IOBufAllocator *allocator =
	  ([] { static ZiMultiplex mx; return ZutcpTest::pktTxAllocator(&mx); })();
	return allocator->allocBuf(nullptr, 0);
      },
      [this, &wire, fail](auto buf, ZiSockAddr addr, Zutcp::EcnMark::T ecn) {
	return Base::sendPathPkt(ZuMv(buf), ZuMv(addr),
	  [&wire, fail](auto packet, ZiSockAddr, Zutcp::EcnMark::T) {
	    wire = ZuMv(packet);
	    return !fail;
	  }, ecn);
      });
  }

#ifdef Zutcp_DEBUG
  void growActivePath(unsigned size) { Base::forceActivePathMTU(size); }
  bool startPMTUDProbe(unsigned size) {
    return Base::startPMTUDProbe_(size);
  }
  void ackPMTUDProbe(unsigned size) {
    Base::ackPMTUDProbe_(size);
  }
  void losePMTUDProbe(unsigned size) {
    Base::losePMTUDProbe_(size);
  }
#endif
  Zutcp::PathDiag pathDiag() const { return Base::pathDiag_(); }
  bool pathValidated() const { return Base::pathValidated_(); }
  uint64_t pathAntiAmp() const {
    return Base::pathAntiAmp_();
  }
  unsigned activePathMaxPayload() const { return Base::activePathMaxPayload_(); }
  Zutcp::LinkDiag diag() const { return Base::diag_(); }
  static constexpr unsigned suspiciousDataThreshold() {
    return Base::SuspiciousDataThreshold;
  }
  unsigned congestionAllowance() const { return Base::congestionAllowance_(); }
  bool ecnDisabled() const { return Base::ecnDisabled_(); }
  void enableECN() { Base::setEcnDisabled_(false); }
  bool ackValid(
    Zutcp::PktNumSpace::T level, uint64_t first, uint64_t largest) const {
    Base::AckSnapshot ack;
    ack.level = level;
    new (ack.ranges.push()) Zutcp::AckRange{largest, first};
    return Base::ackFrameValidTx(ack);
  }
  bool establish(const Zutcp::CxnID &localCID) {
    Base::setRuntimeCIDs_(localCID, localCID, localCID);
    if (!Base::startHandshake()) return false;
    Base::establish_();
    return Base::established();
  }
  bool receiveTCPFrame(const ZmRef<ZiIOBuf> &packet, const Zutcp::TCPHeader &header,
    bool &immediate) {
    Base::RxAckMeta ack;
    ack.tcp = header;
    bool ok = Base::consumeFrames(
      Zutcp::PktNumSpace::AppData, 0, {packet->data(), packet->length}, {},
      packet, ack, nullptr,
      [](Zutcp::PktNumSpace::T, const Zutcp::Frame &, ZiSockAddr) { return true; });
    immediate = ack.immediateAck;
    return ok;
  }
#ifdef Zutcp_DEBUG

#endif

	  bool receiveShort(ZmRef<ZiIOBuf> buf, bool *qlogSeen = nullptr,
    const Zutcp::TCPHeader &tcp = {}) {
    if (!buf) return false;
    Zutcp::Datagram d;
    d.buf = ZuMv(buf);
    d.tcp = tcp;
    return Base::receiveShortPkt(
      d, 0, d.buf->length,
      [this, qlogSeen](
	  Zutcp::PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
		  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
		  Base::RxAckMeta &ack, auto *qlog) {
	if (qlogSeen) *qlogSeen = qlog != nullptr;
	return Base::consumeFrames(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog,

	  [](Zutcp::PktNumSpace::T, const Zutcp::Frame &, ZiSockAddr) {
	    return true;
	  });
	      });
	  }

  unsigned nativeResetAborts = 0;
  bool nativeResetPeer = false;
  void tcpResetAbort_(bool peer) {
    ++nativeResetAborts;
    nativeResetPeer = peer;
    Base::disconnect_(peer, [] {});
  }
  unsigned localResetSends = 0;
  Zutcp::TCPHeader localResetHeader;
  ZmRef<ZiIOBuf> localResetWire;
  bool sendTCPReset_() {
    bool sent = resetWire(localResetHeader, localResetWire);
    localResetSends += unsigned(sent);
    return sent;
  }
  void peerFINForTest() { Base::peerFinTx_(Base::dataGen()); }
  unsigned nativeFinishes = 0;
  void nativeFinished_() { ++nativeFinishes; }
  void acceptedFINAckForTest(uint64_t next) {
    Base::ackSentRx_(Zutcp::PktNumSpace::AppData, 0, next);
  }
  void serverPathForTest(ZiSockAddr local, ZiSockAddr remote) {
    Base::initServerPath_(ZuMv(local), ZuMv(remote));
  }

  bool timerWork() const { return Base::timersActive(); }
  bool resetWire(Zutcp::TCPHeader &tcp, ZmRef<ZiIOBuf> &wire) {
    return Base::sendTCPResetPkt_(
      []() -> ZmRef<Zutcp::PacketBuf> {
	static ZiMultiplex mx;
	auto allocator = mx.allocator<Zi::IOBufType::TxTCP, Zutcp::PacketBuf, Zutcp::BufSize>();
	auto buf = allocator->allocBuf();
	return ZmRef<Zutcp::PacketBuf>{static_cast<Zutcp::PacketBuf *>(buf.ptr())};
      },
      [&tcp, &wire](auto buf) {
	tcp = buf->tcp;
	wire = ZuMv(buf);
	return true;
      });
  }

  bool receivedPN(uint64_t pn) const {
    return Base::rxPktSeen(Zutcp::PktNumSpace::AppData, pn);
  }
  bool windowReply(Zutcp::TCPHeader &header, unsigned &frameBytes) {
    return Base::flushData({},
      [this](Zutcp::PktBuild &build) {
	return Base::appendPendingAck(Zutcp::PktNumSpace::AppData, build);
      },
      [this, &header, &frameBytes](Zutcp::PktBuild &build, ZiSockAddr,
	  const Base::TxPktRefs &refs) {
	header = Base::tcpHeader(&refs);
	frameBytes = build.bytes();
	return Base::sendShortPkt_(CIDSel::Peer, 2, build, ZiSockAddr{}, {}, &refs, false,
	  []() { return Zutcp::inlineAllocator()->allocBuf(); },
	  [](auto, ZiSockAddr, Zutcp::EcnMark::T) { return true; });
      });
  }

  void flushTx_() {
    ++txFlushQueued;
    if (service) {
      service->valid &= app()->mx()->invoked(app()->txThread());
      service->note('N');
      fillCwnd(); // continuously writable ordinary producer consumes credit
    }
  }
  void flushTx_(ZiSockAddr) { ++txFlushQueued; }
  void queueTxFlush() { ++txFlushQueued; }
  void queueTxFlush(ZiSockAddr) { ++txFlushQueued; }
  void pto_() { ++ptos; }
  void queueRetransmit() {
    ++retransmits;
    if (service) {
      ++service->queued;
      app()->txRun([this]() { retransmit_(); });
    }
  }
  bool retransmit_(
    bool pto = false, unsigned maxPackets = 2,
    Zutcp::PktNumSpace::T onlySpace = Zutcp::PktNumSpace::N) {
    ++retransmits;
    ++retransmitCalls;

    if (!service) return false;
    service->valid &= app()->mx()->invoked(app()->txThread());
    Zutcp::PktNumSpace::T level;
    Zutcp::SentFrameRef ref;
    if (!Base::nextRetransmit_(level, ref)) return false;
    if (!pto && !Base::congestionAllowance_()) {
      ++service->blocked;
      Base::requeueRetransmit_(level, ref);
      return false;
    }
    service->valid &= ref.kind == Zutcp::SentFrameKind::Data &&
      ref.range.storage->pending.ptr() == service->origin && ref.length == 8 &&
      !memcmp(ref.range.storage->data + ref.range.offset, "retained", 8);
    service->note('R');
    service->recoveredPN = sentPkts;
    recordSentPkt(level, sentPkts++, 100, ref, true);
    ++service->recovered;
    return true;
  }


  uint64_t		sentPkts = 0;
  unsigned		txFlushQueued = 0;
  unsigned		ptos = 0;
  unsigned		probes = 0;
  bool			probeValid = true, probeAllowed = true;
  unsigned processed = 0;
  ZmFn<void()> dequeued;
  unsigned		retransmits = 0;
  unsigned		retransmitCalls = 0;
  bool			directPassive = false;
  unsigned		passiveSends = 0;
  ZiSockAddr		passiveRemote;
};

static ZmRef<TestLink> testLink(App *app, bool isServer = false)
{
  ZmRef<TestLink> link = new TestLink{app, isServer};
  link->resetRuntimeForTest();
  return link;
}

#ifdef Zutcp_DEBUG
static ZmRef<ZiIOBuf> shortPing(
  const Zutcp::CxnID &dcid, uint64_t pn)
{
  static ZiMultiplex mx;
  auto buf = ZutcpTest::pktTxAllocator(&mx)->allocBuf();
  if (!buf) return {};
  int header = Zutcp::Pkt::writeShort(buf->data(), buf->avail(), dcid, pn, 2);
  if (header < 0 || Zutcp::FrameCodec::writePing(
      buf->data() + header, buf->avail() - unsigned(header)) != 1) return {};
  buf->length = unsigned(header) + 1;
  return buf;
  }

#endif

static bool parseFrame(const uint8_t *b, int n, Zutcp::Frame &frame)
{
  unsigned used = 0;
  return n > 0 && !Zutcp::FrameCodec::parse(
    ZuBSpan{b, unsigned(n)}, frame, used) &&
    used == unsigned(n);
}

static ZmRef<ZiIOBuf> dataPkt(uint64_t offset, ZuBSpan payload, bool fin, Zutcp::Frame &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return ZutcpTest::pktRxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
  if (ZuUnlikely(!packet)) return nullptr;
  int n = Zutcp::FrameCodec::writeData(
    packet->data_(), packet->size, offset, payload, fin);
  if (n <= 0) return nullptr;
  packet->skip = 0;
  packet->length = unsigned(n);
  if (Zutcp::FrameCodec::parse(
      ZuBSpan{packet->data(), packet->length}, frame, used) ||
      used != packet->length)
    return nullptr;
  return packet;
}

static bool consumeExact_(Zutcp::Reader &rx, unsigned n, ZuBSpan expected)
{
  unsigned remaining = n;
  bool called = false;
  bool ok = false;
  int64_t consumed = rx.consume(
    [&remaining](ZuSpan<uint8_t> span) -> int64_t {
      if (remaining > span.length()) {
	remaining -= span.length();
	return 0;
      }
      return remaining;
    },
    [&called, &ok, &expected](ZuSpan<uint8_t> span) {
      called = true;
      ok = span.length() == expected.length() &&
	!memcmp(span.data(), expected.data(), expected.length());
    });
  return consumed == n && called && ok;
}

#ifdef Zutcp_DEBUG
static ZiIP ip4_(uint32_t n)
{
  in_addr addr;
  addr.s_addr = htonl(n);
  return ZiIP{addr};
}

static Zi::Path testPath(ZuCSpan name)
{
  return ZiFile::append(g_residue, name);
}

static ZtString<> readFile_(const Zi::Path &path)
{
  ZtString<> data;
  ZiFile file{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return data;
  auto size = file.size();
  if (size <= 0 || size > (1<<20)) return data;
  data.length(unsigned(size));
  int n = file.read(data.data(), data.length());
  if (n <= 0)
    data.length(0);
  else
    data.length(unsigned(n));
  return data;
}

static unsigned parseJSONSeq(ZuCSpan data)
{
  unsigned n = 0;
  unsigned i = 0, l = data.length();
  while (i < l) {
    ZuCSpan rest{data.data() + i, l - i};
    if (!rest.match<"\x1e">()) return 0;
    ++i;
    unsigned start = i;
    while (i < l && data[i] != '\n') ++i;
    if (i >= l) return 0;
    ZtString<> json;
    json << ZuCSpan{data.data() + start, i - start};
    auto scan = ZfJSON::scan(json);
    if (scan.p<0>() != int(json.length())) return 0;
    ++n;
    ++i;
  }
  return n;
}
#endif

void testDataFrameDelivery()
{
  ZuTestScope(testDataFrameDelivery);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto cxn = client;

  Zutcp::Frame frame;
  unsigned used = 0;
  auto packet = dataPkt(0, "hello", false, frame, used);
  ZuCHECK(packet, "DATA frame setup failed");

  Zutcp::BufDiag diag;
  ZuCHECK(cxn->processFrame(frame, packet, &diag) == 0 &&
      cxn->processed == 1 &&
      cxn->rxBytes() == 5,
    "DATA frame delivery mismatch");

  auto &rx = cxn->reader();
  ZuCHECK(consumeExact_(rx, 5, "hello"),
    "DATA payload was not queued for receive");
  ZuCHECK(rx.empty(), "DATA receive queue consume failed");

  packet = dataPkt(5, {}, true, frame, used);
  ZuCHECK(packet, "DATA FIN frame setup failed");
  ZuCHECK(cxn->processFrame(frame, packet, &diag) == 0 &&
      cxn->finReceived() && cxn->rxComplete() &&
      cxn->finalSize() == 5,
    "DATA FIN delivery mismatch");

  packet = dataPkt(5, "!", true, frame, used);
  ZuCHECK(packet,
    "conflicting DATA FIN setup failed");
  ZuCHECK(!cxn->receiveDataFrame(frame, packet, &diag),
    "conflicting final-size DATA was accepted");

}

void testDataRxSliceDelivery()
{
  ZuTestScope(testDataRxSliceDelivery);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto cxn = client;

  ZmRef<ZiIOBuf> packet = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return ZutcpTest::pktRxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
  ZuCHECK(bool(packet), "packet buffer allocation failed");
  if (!packet) return;
  int n = Zutcp::FrameCodec::writeData(
    packet->data_(), packet->size, 0, "slice", false);
  ZuCHECK(n > 0, "packet-backed DATA frame write failed");
  packet->skip = 0;
  packet->length = unsigned(n);

  Zutcp::Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zutcp::FrameCodec::parse(
      ZuBSpan{packet->data(), packet->length}, frame, used) &&
      used == packet->length,
    "packet-backed DATA frame parse failed");

  Zutcp::BufDiag diag;
  ZuCHECK(cxn->processFrame(frame, packet, &diag) == 0 &&
      cxn->processed == 1 &&
      cxn->rxBytes() == 5,
    "packet-backed DATA slice mismatch");
  packet = nullptr;

  auto &rx = cxn->reader();
  ZuCHECK(consumeExact_(rx, 5, "slice"),
    "packet-backed DATA slice payload mismatch");
  ZuCHECK(rx.empty(), "packet-backed DATA slice consume failed");
}

void testOutOfOrderDataDelivery()
{
  ZuTestScope(testOutOfOrderDataDelivery);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto cxn = client;

  Zutcp::Frame frame;
  unsigned used = 0;
  Zutcp::BufDiag diag;

  auto packet = dataPkt(5, "world", false, frame, used);
  ZuCHECK(packet,
    "out-of-order DATA setup failed");
  ZuCHECK(cxn->processFrame(frame, packet, &diag) == 0 &&
      cxn->processed == 1 &&
      !cxn->rxBytes() &&
      cxn->rxPending() == 1 &&
      !cxn->rxQueued(),
    "out-of-order DATA pending state mismatch");
  ZuCHECK(cxn->processFrame(frame, packet, &diag) == 0 &&
      cxn->rxPending() == 1,
    "duplicate pending DATA copied or queued again");

  packet = dataPkt(0, "hello", false, frame, used);
  ZuCHECK(packet,
    "gap-filling DATA setup failed");
  ZuCHECK(cxn->processFrame(frame, packet, &diag) == 0 &&
      cxn->rxBytes() == 10 &&
      !cxn->rxPending() &&
      cxn->rxQueued() == 2,
    "gap-filling DATA did not drain pending data");

  auto &rx = cxn->reader();
  ZuCHECK(consumeExact_(rx, 5, "hello"),
    "first reordered DATA payload mismatch");
  ZuCHECK(consumeExact_(rx, 5, "world"),
    "second reordered DATA payload mismatch");
  ZuCHECK(rx.empty(), "second reordered DATA consume failed");

  auto split = testLink(client->app(), client->isServer());
  diag = {};
  packet = dataPkt(5, "world", false, frame, used);
  ZuCHECK(packet,
    "split pending DATA setup failed");
  ZuCHECK(split->processFrame(frame, packet, &diag) == 0 &&
      split->rxPending() == 1 &&
      !split->rxQueued(),
    "split pending DATA state mismatch");

  packet = dataPkt(0, "helloworldtails", false, frame, used);
  ZuCHECK(packet,
    "interior-overlap DATA setup failed");
  ZuCHECK(split->processFrame(frame, packet, &diag) == 0 &&
      split->rxBytes() == 15 &&
      !split->rxPending() &&
      split->rxQueued() == 3,
    "interior-overlap DATA did not copy only novel spans");

  auto &splitRx = split->reader();
  ZuCHECK(consumeExact_(splitRx, 5, "hello"),
    "split first DATA payload mismatch");
  ZuCHECK(consumeExact_(splitRx, 5, "world"),
    "split second DATA payload mismatch");
  ZuCHECK(consumeExact_(splitRx, 5, "tails"),
    "split third DATA payload mismatch");
  ZuCHECK(splitRx.empty(), "split DATA consume failed");
}

void testRxRetirement()
{
  ZuTestScope(testRxRetirement);

  App app;
  Zutcp::Frame frame;
  unsigned used = 0;

  ZmRef<TestLink> stalled = testLink(&app, true);
  auto packet = dataPkt(0, "abc", false, frame, used);
  ZuCHECK(packet && stalled->receiveFrame(frame, packet) == 0,
    "stalled receive retirement setup failed");
  auto cxn = stalled;
  ZuCHECK(cxn && cxn->reader().length() == 3 &&
      cxn->rxRetirable() == 3 && !cxn->rxRetired() &&
      stalled->rxDataCreditUsed() == 3 && !stalled->rxDataRetired(),
    "native delivery returned QUIC receive credit prematurely");

  cxn->reader().advance(3);
  ZuCHECK(!cxn->reader() && cxn->rxRetirable() == 3 &&
      !cxn->rxRetired() && !stalled->rxDataRetired(),
    "native queue removal returned QUIC receive credit implicitly");
  ZuCHECK(cxn->retireRx(2) && cxn->rxRetired() == 2 &&
      stalled->rxDataRetired() == 2 &&
      !cxn->retireRx(2) && cxn->retireRx(1) &&
      cxn->rxRetired() == 3 && stalled->rxDataRetired() == 3,
    "explicit QUIC receive retirement was not exact");

  {
    App small{Zutcp::MinPktPayload, 8};
    auto credit = testLink(&small, true);
    packet = dataPkt(0, "abcdef", false, frame, used);
    ZuCHECK(packet && credit->receiveFrame(frame, packet) == 0,
      "small-window receive retirement setup failed");
    auto receiver = credit;
    unsigned queued = credit->txFlushQueued;
    ZuCHECK(receiver && receiver->retireRx(6) &&
	credit->rxDataCreditLimit() == 14 && receiver->rxCreditLimit() == 14 &&
	credit->txFlushQueued == queued + 1,
      "retired bytes schedule connection credit flushes without application writes");
  }

  ZmRef<TestLink> reset = testLink(&app, true);
  packet = dataPkt(3, "de", false, frame, used);
  ZuCHECK(packet && reset->receiveFrame(frame, packet) == 0,
    "out-of-order receive retirement setup failed");
  cxn = reset;
  ZuCHECK(cxn && cxn->rxAdmitted() == 2 && !cxn->rxBytes() &&
      !cxn->rxRetirable() && !cxn->retireRx(1) &&
      reset->rxDataCreditUsed() == 5 && !reset->rxDataRetired(),
    "out-of-order QUIC bytes became retirable across a gap");

  uint8_t b[32];
  int n = Zutcp::FrameCodec::writeReset(
    b, sizeof(b), 7, 5);
  ZuCHECK(n > 0 && !Zutcp::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used) &&
      reset->receiveFrame(frame) == 0 &&
      cxn->resetReceived() && cxn->finalSize() == 5 &&
      cxn->rxRetired() == 5 && reset->rxDataCreditUsed() == 5 &&
      reset->rxDataRetired() == 5,
    "RESET_DATA did not admit and retire its exact final-size gap");
}

void testDataTxRetention()
{
  ZuTestScope(testDataTxRetention);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto cxn = client;

  {
    auto tx = cxn->writer_();
    tx << "abc" << Zi::flush();
    tx << "def" << Zi::flush();
  }
  cxn->fin();

  ZuCHECK(cxn->txBytes() == 6 &&
      cxn->txBufferedBytes() == 6 &&
      cxn->txRangeCount() == 2 &&
      cxn->finSent() &&
      !cxn->finReady() &&
      !cxn->finDequeued(),
    "connection Tx retention counters mismatch");

  uint64_t finOffset = ~uint64_t{0};
  ZuCHECK(!cxn->dequeueFin(finOffset),
    "FIN dequeued before retained data drained");

  Zutcp::TxRange range;
  ZuCHECK(cxn->txRange(0, range) &&
      range.storage && !range.offset && range.length == 3 &&
      !range.dataOffset &&
      !memcmp(range.storage->data + range.offset, "abc", 3),
    "first retained Tx range mismatch");
  ZuCHECK(cxn->dequeueTxRange(range) &&
      range.length == 3 &&
      !range.dataOffset &&
      !memcmp(range.storage->data + range.offset, "abc", 3) &&
      cxn->txBufferedBytes() == 3 &&
      cxn->txRangeCount() == 1,
    "first Tx range dequeue mismatch");
  ZuCHECK(cxn->dequeueTxRange(range) &&
      range.length == 3 &&
      range.dataOffset == 3 &&
      !memcmp(range.storage->data + range.offset, "def", 3) &&
      !cxn->txBufferedBytes() &&
      !cxn->txRangeCount() &&
      cxn->finReady(),
    "second Tx range dequeue mismatch");
  ZuCHECK(cxn->dequeueFin(finOffset) &&
      finOffset == 6 &&
      cxn->finDequeued() &&
      !cxn->finReady(),
    "FIN dequeue state mismatch");
  ZuCHECK(!cxn->dequeueFin(finOffset),
    "FIN dequeued more than once");
  ZuCHECK(!cxn->dequeueTxRange(range),
    "empty Tx range dequeue succeeded");
}

void testDataTxRetainedBuffers()
{
  ZuTestScope(testDataTxRetainedBuffers);

  App app;
  auto link = testLink(&app);
  auto cxn = link;
  auto tx = cxn->writer_();
  auto backing = tx.allocBuf(7);
  backing->append(reinterpret_cast<const uint8_t *>("A-gap-BC"), 8);
  auto original = backing->data();
  ZmRef<ZiIOBuf> first = new Zutcp::DataRxPQueue::Node{
    backing, original, 1, app.dataRxAllocator(), nullptr, 0};
  ZmRef<ZiIOBuf> second = new Zutcp::DataRxPQueue::Node{
    backing, original + 6, 2, app.dataRxAllocator(), nullptr, 0};
  auto firstView = first.ptr(), secondView = second.ptr();
  ZuCHECK(tx.appendBuf(ZuMv(first)) && tx.appendBuf(ZuMv(second)) &&
      !first && !second,
    "QUIC narrowed buffer ownership transfer failed");
  tx << "D" << Zi::Flush{};
  ZuCHECK(cxn->txBytes() == 4 && cxn->txRangeCount() == 3,
    "QUIC owned buffer and ordinary node connection offsets mismatch");
  Zutcp::TxRange selected;
  Zutcp::TxRange consumed;
  ZuCHECK(cxn->txRange(0, selected) && selected.storage->pending.ptr() == firstView &&
      selected.storage->data + selected.offset == original && selected.length == 1 &&
      cxn->commitTxRange(consumed, selected, 1),
    "QUIC first narrowed buffer lost original backing");
  ZuCHECK(cxn->txRange(0, selected) && selected.storage->pending.ptr() == secondView &&
      selected.storage->data + selected.offset == original + 6 && selected.dataOffset == 1 &&
      cxn->commitTxRange(consumed, selected, 1) &&
      cxn->txRange(0, selected) && selected.storage->data + selected.offset == original + 7 &&
      selected.length == 1 && selected.dataOffset == 2,
    "QUIC partial buffer consumption copied or lost positions");
  ZuCHECK(selected.offset == 1 && selected.storage->pending->skip == 0 &&
      selected.storage->pending->length == 2,
    "queued range consumption mutated the original descriptor coordinates");
  ZuCHECK(backing->length == 8 && backing->skip == 7 &&
      !memcmp(backing->data(), "A-gap-BC", 8),
    "QUIC narrowed descriptors must retain original plaintext bytes");
  cxn->reset(0);
  ZuCHECK(!cxn->txRangeCount() && !cxn->txQueueCount() &&
      selected.storage->data[selected.offset] == 'C',
    "queue reset invalidated a retained range's original backing");
}

void testFinAdmissionHandoff()
{
  ZuTestScope(testFinAdmissionHandoff);
  App app;
  auto link = testLink(&app);
  unsigned errors = 0;
  link->txErrorFn(ZiTxErrorFn{[&errors](ZeException &) { ++errors; return false; }});
  link->txQueueMax(1);
  app.holdTxInvoke = app.producer = true;
  auto buf = Zutcp::inlineAllocator()->allocBuf();
  memcpy(buf->data_(), "original", 8);
  buf->length = 8;
  auto original = buf->data_();
  bool accepted = link->enqueue(buf);
  app.producer = false;
  ZuCHECK(accepted && link->txQueueCount() == 1 && !link->txRangeCount(),
    "off-owner admission must reserve before its Tx handoff is delivered");
  link->closeTxAdmission();
  app.producer = false;
  link->fin();
  ZuCHECK(!link->finReady() && !link->txDrained() && !link->finDequeued(),
    "FIN must wait for admitted work not yet present in the Tx range queue");
  auto denied = Zutcp::inlineAllocator()->allocBuf();
  denied->length = 1;
  app.producer = true;
  ZuCHECK(!link->enqueue(denied) && !errors && !link->disconnecting_() &&
      link->txQueueCount() == 1,
    "closed producer admission must refuse benignly even when queue capacity is occupied");
  app.runPendingTx();
  Zutcp::TxRange range;
  ZuCHECK(link->txRangeCount() == 1 && link->txRange(0, range) &&
      range.storage->data == original && range.storage->payloadLength == 8 &&
      range.storage->pending.ptr() == buf.ptr() &&
      !memcmp(range.storage->data + range.offset, "original", 8),
    "a pre-admitted handoff must preserve its original backing after the close gate");
  ZuCHECK(link->dequeueTxRange(range) && !link->txQueueCount() && link->finReady() &&
      link->txDrained() && !errors,
    "only retirement of the admitted source may make FIN ready");
  app.holdTxInvoke = false;
  link->teardown();
}

void testDataTxQueueLimit()
{
  ZuTestScope(testDataTxQueueLimit);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto cxn = link;
  cxn->txQueueMax(1);
  unsigned errors = 0;
  cxn->txErrorFn(ZiTxErrorFn{
    [&errors](ZeException &) {
      ++errors;
      return true;
    }});

  auto tx1 = cxn->writer_();
  tx1 << "one" << Zi::flush();
  ZuCHECK(!tx1.failed() && cxn->txQueueCount() == 1,
    "first QUIC connection Tx buffer was not admitted");

  auto tx2 = cxn->writer_();
  tx2 << "two" << Zi::flush();
  ZuCHECK(tx2.failed() && errors == 1 && cxn->txQueueCount() == 1,
    "QUIC connection Tx queue overflow was not reported");

  Zutcp::TxRange range;
  ZuCHECK(cxn->dequeueTxRange(range) && !cxn->txQueueCount(),
    "QUIC connection Tx admission was not released on dequeue");

  auto tx3 = cxn->writer_();
  tx3 << "three" << Zi::flush();
  ZuCHECK(!tx3.failed() && cxn->txQueueCount() == 1 && errors == 1,
    "QUIC connection Tx queue did not reopen after dequeue");
}

void testDataTxUnackd()
{
  ZuTestScope(testDataTxUnackd);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto cxn = link;
  ZuCHECK(cxn &&
      cxn->recordTxUnackd(10, 10, false) &&
      cxn->txUnackdCount() == 1 &&
      cxn->txUnackdBytes() == 10,
    "connection unackd range setup failed");

  ZuCHECK(cxn->ackTxUnackd(13, 4) &&
      cxn->txStillUnackd(10, 3, false) &&
      !cxn->txStillUnackd(13, 4, false) &&
      cxn->txStillUnackd(17, 3, false) &&
      cxn->txUnackdCount() == 2 &&
      cxn->txUnackdBytes() == 6,
    "connection unackd partial ACK did not split state");

  ZuCHECK(cxn->ackTxUnackd(10, 3, false) &&
      !cxn->txStillUnackd(10, 3, false) &&
      cxn->txStillUnackd(17, 3, false) &&
      cxn->txUnackdCount() == 1 &&
      cxn->txUnackdBytes() == 3,
    "connection unackd exact ACK did not remove range");
}

void testDataTxUnackdFin()
{
  ZuTestScope(testDataTxUnackdFin);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto cxn = link;
  ZuCHECK(cxn &&
      cxn->recordTxUnackd(20, 5, true) &&
      cxn->txUnackdCount() == 1 &&
      cxn->txUnackdBytes() == 6,
    "connection unackd data+FIN setup failed");

  ZuCHECK(cxn->ackTxUnackd(20, 5, false) &&
      !cxn->txStillUnackd(20, 5, false) &&
      cxn->txStillUnackd(25, 0, true) &&
      cxn->txUnackdCount() == 1 &&
      cxn->txUnackdBytes() == 1,
    "connection unackd data ACK did not retain FIN");

  ZuCHECK(cxn->ackTxUnackd(25, 0, true) &&
      !cxn->txStillUnackd(25, 0, true) &&
      !cxn->txUnackdCount() &&
      !cxn->txUnackdBytes(),
    "connection unackd FIN ACK did not clear sentinel");
}

struct RawRetxAllocator : Zi::IOBufAllocator {
  alignas(ZiIOBuf_Align) uint8_t bytes[512];
  mutable unsigned returned = 0;
  mutable bool valid = true;
  explicit RawRetxAllocator(ZiMultiplex *mx = nullptr) :
    IOBufAllocator{mx, Zi::IOBufType::TxTCP, 512, 256} {
    memset(bytes, 0xa5, sizeof(bytes));
  }
  uint8_t *alloc(unsigned) const override { return nullptr; }
  void free(uint8_t *p, unsigned n, bool registered) const override {
    valid &= p == bytes && n == sizeof(bytes) && registered;
    ++returned;
  }
  ZmRef<ZiIOBuf> allocBuf(void *, uint32_t, uint32_t) override { return nullptr; }
};

void testAbortAdmissionHandoff()
{
  ZuTestScope(testAbortAdmissionHandoff);
  App app;
  auto link = testLink(&app);
  RawRetxAllocator allocator{app.mx()};
  ZmRef<ZiIOBuf> buf = new Zi::IOBufStub<ZiIOBuf>{
    allocator.bytes, unsigned(sizeof(allocator.bytes)), &allocator, nullptr,
    unsigned(8), false, true};
  buf->skip = 256;
  memcpy(allocator.bytes + 256, "original", 8);
  unsigned errors = 0;
  link->txErrorFn(ZiTxErrorFn{[&errors](ZeException &) { ++errors; return false; }});
  auto generation = link->dataGen();
  app.holdTxInvoke = app.producer = true;
  bool accepted = link->enqueue(ZuMv(buf));
  app.producer = false;
  unsigned flushes = link->txFlushQueued;
  ZuCHECK(accepted && link->txQueueCount() == 1 && !allocator.returned,
    "an accepted producer handoff must retain its original backing before owner delivery");
  link->teardown(); // Complete the actual Core drain without delivering the handoff.
  app.runPendingTx();
  ZuCHECK(link->dataGen() == generation && !link->txQueueCount() && !link->txRangeCount() &&
      !errors && link->txFlushQueued == flushes && allocator.valid && allocator.returned == 1 &&
      !memcmp(allocator.bytes + 256, "original", 8),
    "a late current-generation handoff after abort must cancel without ranges or protocol work");
  app.holdTxInvoke = false;
}

void registeredTxBoundary()
{
  ZuTestScopeRT(registeredTxBoundary);
  App app;
  app.requireNativeTx = true;
  auto link = testLink(&app);
  auto heap = Zutcp::inlineAllocator()->allocBuf(nullptr, 8);
  auto original = heap->data_();
  ZuCHECK(!link->enqueue_(heap) && !link->txRangeCount() && !link->txQueueCount() &&
      heap->data_() == original && heap->length == 8,
    "native admission must reject heap storage without retaining work or detaching its owner");
  ZiMultiplex other;
  RawRetxAllocator foreign{&other};
  ZmRef<ZiIOBuf> packet = new Zi::IOBufStub<ZiIOBuf>{
    foreign.bytes, unsigned(sizeof(foreign.bytes)), &foreign, nullptr,
    unsigned(8), false, true};
  packet->skip = 256;
  ZuCHECK(!link->enqueue_(packet) && !link->txRangeCount() && !link->txQueueCount() &&
      packet->data_() == foreign.bytes && packet->length == 8 && !foreign.returned,
    "native admission must reject another mx's registered backing without consuming its lease");
  packet = nullptr;
  ZuCHECK(foreign.valid && foreign.returned == 1,
    "rejected registered backing must return through its original owner once");
  link->teardown();
}

void registeredCoveringRetx(bool fin)
{
  ZuTestScopeRT(registeredCoveringRetx);
  App app;
  auto link = testLink(&app);
  RawRetxAllocator allocator;
  Zxdp::BufPool pool{1, sizeof(allocator.bytes)};
  ZmRef<Zutcp::TxBacking> source = new Zutcp::TxRanges::Node{};
  source->native = &allocator;
  source->pool = &pool;
  source->data = allocator.bytes;
  source->size = sizeof(allocator.bytes);
  source->payloadOffset = 256;
  source->payloadLength = source->preparedLength = 32;
  source->backing.adopt(&allocator, allocator.bytes, sizeof(allocator.bytes));
  Zutcp::SentFrameRef ref = Zutcp::SentFrameRef::data(
    Zutcp::TxRange{source, 256, 32, 0}, fin);
  link->initServerPath(ZiSockAddr{ZiIP{"198.18.0.10"}, 18192},
    ZiSockAddr{ZiIP{"198.18.0.11"}, 18191});
  link->validatePath();
  ZuCHECK(link->establish({}), "registered recovery fixture must establish");
  unsigned allocations = 0, sent = 0;
  auto payload = allocator.bytes + 256;
  ZuCHECK(link->rawRetx(ref, allocations, sent, payload) && sent == 1 && !allocations,
    "retained raw backing must submit at its original payload address without allocation");
  uint8_t original[sizeof(allocator.bytes)];
  memcpy(original, allocator.bytes, sizeof(original));
  app.rawWritable = false;
  ZuCHECK(!link->rawRetx(ref, allocations, sent, payload) && sent == 1 && !allocations &&
      !memcmp(original, allocator.bytes, sizeof(original)),
    "outstanding NIC use must prevent header changes and replacement allocation");
  link->ackBytes(8, 0);
  link->reclaimData(8);
  Zutcp::PktNumSpace::T level;
  Zutcp::SentFrameRef covering;
  bool selected = link->nextRetransmitRef(level, covering);
  ZuCHECK(selected && covering.offset == 0 && covering.length == 32 &&
      covering.range.offset == 256 && covering.range.storage == source &&
      link->txAckd() == 8 && !allocator.returned && *source->backing,
    "an unaligned cumulative ACK must retain and select the complete covering packet");
  app.rawWritable = true;
  ZuCHECK(selected && link->rawRetx(covering, allocations, sent, payload) &&
      sent == 2 && !allocations && !memcmp(payload, original + 256, 32),
    "retirement must permit sequential resubmission without moving retained payload");
  link->ackBytes(32, 1);
  if (fin) {
    ZuCHECK(!allocator.returned && *source->backing && source->data &&
	link->txStillUnackd(32, 0, true),
      "payload ACK without its attached FIN must keep the original covering backing");
    link->reclaimData(32);
    bool finSelected = link->nextRetransmitRef(level, covering);
    ZuCHECK(finSelected && covering.offset == 0 && covering.length == 32 && covering.fin &&
	link->rawRetx(covering, allocations, sent, payload) && sent == 3 && !allocations,
      "a missing FIN must resend its original packet including duplicate payload edges");
    link->ackBytes(33, 2);
  }
  ZuCHECK(allocator.valid && allocator.returned == 1 && !*source->backing && !source->data,
    "final byte ACK must return the original allocation once despite retained packet history");
  link->teardown();
}

void byteAckRTT()
{
  ZuTestScopeRT(byteAckRTT);
  App app;
  app.testNow = Zm::now() + ZuTime{10};
  ZuTime start = app.testNow;
  auto link = testLink(&app);
  bool ready = link->establish({});
  ZuCHECK(ready, "byte RTT fixture must establish");
  if (!ready) { link->teardown(); return; }
  Zutcp::TransportParams peer;
  peer.initialMaxData = 1024;
  peer.maxAckDelay = 25;
  peer.ackDelayExponent = 3;
  link->peerParams(peer);
  ZuCHECK(link->ackDelay_(1250) == Zutcp::timeUS(10000),
    "fixture must install a real peer ACK-delay policy before checking DATA timing");
  auto ref = link->prepareDataRef(0, 0, 32);
  link->acceptTxData(ref);
  app.testNow = start + Zutcp::timeUS(100000);
  link->ackBytes(32, 0);
  ZuTime seed = Zutcp::timeUS(112500);
  ZuCHECK(link->txAckd() == 32 && link->lossThreshold() == seed,
    "a fresh cumulative byte ACK must sample the actual 100 ms flight");
  app.testNow = start + Zutcp::timeUS(110000);
  ref = link->prepareDataRef(1, 32, 32);
  link->acceptTxData(ref);
  app.testNow = start + Zutcp::timeUS(160000);
  ref = link->prepareDataRef(2, 32, 32);
  link->acceptTxData(ref);
  app.testNow = start + Zutcp::timeUS(230000);
  link->ackBytes(64, 2);
  ZuCHECK(link->txAckd() == 64 && link->lossThreshold() == seed,
    "an ACK after retransmission must not sample either ambiguous send time");
  app.testNow = start + Zutcp::timeUS(240000);
  ref = link->prepareDataRef(3, 64, 16);
  link->acceptTxData(ref);
  app.testNow = start + Zutcp::timeUS(260000);
  link->ackBytes(80, 3);
  ZuCHECK(link->txAckd() == 80 && link->lossThreshold() == Zutcp::timeUS(101250),
    "fresh DATA beyond the acknowledged ambiguity fence must resume RTT sampling");
  app.testNow = start + Zutcp::timeUS(270000);
  ref = link->prepareDataRef(4, 80, 16);
  link->acceptTxData(ref);
  app.testNow = start + Zutcp::timeUS(350000);
  link->ackBytes(96, 4, 1250);
  ZuCHECK(link->txAckd() == 96 && link->lossThreshold() == (ZuTime{0, 99843750}),
    "DATA RTT must use the raw 80 ms flight without subtracting the peer's 10 ms ACK delay");
  link->teardown();
}

void cumulativeAckAuthority()
{
  ZuTestScope(cumulativeAckAuthority);
  App app;
  auto link = testLink(&app);
  auto ref = link->prepareDataRef(0, 0, 10);
  link->ackBytes(10, 0);
  ZuCHECK(!link->txNext() && !link->txAckd() && link->txUnackdBytes() == 10,
    "prepared packet metadata is not evidence of accepted byte transmission");
  link->acceptTxData(ref);
  link->ackOnly(0);
  ZuCHECK(link->txNext() == 10 && !link->txAckd() && link->txUnackdBytes() == 10,
    "packet acknowledgment cannot retire DATA storage");
  link->ackBytes(4, 0);
  ZuCHECK(link->txAckd() == 4 && link->txUnackdBytes() == 6 &&
      link->txStillUnackd(4, 6) && !link->txStillUnackd(0, 4),
    "cumulative bytes retire only the acknowledged prefix");
  link->ackBytes(11, 0);
  ZuCHECK(link->txAckd() == 4 && link->txUnackdBytes() == 6,
    "ACK beyond accepted sequence space cannot release backing");
  link->ackBytes(10, 0);
  ZuCHECK(link->txAckd() == 10 && !link->txUnackdBytes(),
    "the final cumulative byte ACK retires the remaining range");
  link->teardown();
}

void testLinkDataTxUnackdAck()
{
  ZuTestScope(testLinkDataTxUnackdAck);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto cxn = link;

  link->sendDataRef(7, 40, 5);
  ZuCHECK(cxn->txUnackdCount() == 1 &&
      cxn->txUnackdBytes() == 5 &&
      cxn->txStillUnackd(40, 5),
    "sent DATA ref did not populate unackd state");

  link->ackBytes(45, 7);
  ZuCHECK(!cxn->txUnackdCount() &&
      !cxn->txUnackdBytes() &&
      !cxn->txStillUnackd(40, 5),
    "ACKd DATA ref did not clear unackd state");
  link->teardown();
}

void dataRetxUnackdIdempotent()
{
  ZuTestScope(dataRetxUnackdIdempotent);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto cxn = link;

  link->sendDataRef(0, 40, 5);
  ZuCHECK(cxn->txUnackdCount() == 1 &&
      cxn->txUnackdBytes() == 5 &&
      cxn->txStillUnackd(40, 5),
    "sent DATA ref did not populate unackd state");

  link->sendAckEliciting(4);
  link->ackOnly(4);
  link->reclaimData(40);

  Zutcp::PktNumSpace::T level = Zutcp::PktNumSpace::Initial;
  Zutcp::SentFrameRef ref;
  ZuCHECK(link->nextRetransmitRef(level, ref) &&
      level == Zutcp::PktNumSpace::AppData &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      ref.offset == 40 &&
      ref.length == 5 &&
      !ref.fin,
    "lost DATA ref was not queued for retransmit");

  link->sendDataRef(5, ref.offset, ref.length, ref.fin);
  ZuCHECK(cxn->txUnackdCount() == 1 &&
      cxn->txUnackdBytes() == 5 &&
      cxn->txStillUnackd(40, 5),
    "retransmitted DATA ref duplicated unackd owner state");

  link->sendAckEliciting(9);
  link->ackOnly(9);
  link->reclaimData(40);
  ZuCHECK(cxn->txUnackdCount() == 1 &&
      cxn->txUnackdBytes() == 5 &&
      cxn->txStillUnackd(40, 5),
    "lost retransmitted DATA ref changed owner state");

  link->ackBytes(45, 5);
  ZuCHECK(!cxn->txUnackdCount() &&
      !cxn->txUnackdBytes() &&
      !cxn->txStillUnackd(40, 5),
    "late ACK of retained lost retransmit did not clear owner state");

  link->ackOnly(0);
  ZuCHECK(!cxn->txUnackdCount() &&
      !cxn->txUnackdBytes(),
    "late ACK of retained lost DATA ref changed owner state");
  link->teardown();
}

void testDataRetxPacketBudget()
{
  ZuTestScope(testDataRetxPacketBudget);

#ifdef Zutcp_DEBUG
  App app{Zutcp::BufSize};
  auto link = testLink(&app, true);
  link->initServerPath();
  link->validatePath();
  link->growActivePath(1472);
  ZuCHECK(link->establish({}),
    "retransmit packet budget plaintext setup failed");
  constexpr unsigned Size = 1300;
  uint8_t expected[Size], decoded[Size]{};
  unsigned coverage[Size]{};
  for (unsigned i = 0; i < Size; ++i) expected[i] = uint8_t(i * 13 + 7);
  auto cxn = link;
  cxn->txCredit(Size);
  link->grantDataCredit(Size);
  {
    auto tx = cxn->writer_();
    tx << span_(expected, Size) << Zi::flush();
  }
  cxn->fin();
  Zutcp::SentFrameRef original;
  unsigned n = link->flushRecordedRefs(&original, 1);
  bool ready = n == 1 && original.kind == Zutcp::SentFrameKind::Data &&
    original.range.storage && original.length == Size && original.fin;
  ZuCHECK(ready, "old MTU packet did not retain the complete DATA plus FIN");
  if (!ready) { link->teardown(); return; }
  auto backing = original.range.storage;
  link->sendAckEliciting(4);
  link->ackOnly(4);
  link->reclaimData();
  Zutcp::PktNumSpace::T lostLevel;
  Zutcp::SentFrameRef lost;
  bool reclaimed = link->nextRetransmitRef(lostLevel, lost) &&
    lostLevel == Zutcp::PktNumSpace::AppData && lost.range.storage == backing &&
    lost.offset == original.offset && lost.length == original.length && lost.fin;
  ZuCHECK(reclaimed, "old packet loss did not yield its actual retained recovery reference");
  if (!reclaimed) { link->teardown(); return; }
  original = ZuMv(lost);
  uint64_t credit = cxn->txCreditAvailable();
  link->initServerPath(); // A promoted path starts at the 1200-byte minimum.
  link->validatePath();
  link->sparseAck();
  Zutcp::SentFrameRef head = original;
  ZmRef<ZiIOBuf> wire;
  bool withAck = false;
  bool sent = link->dataRetx(head, wire, true, withAck);
  bool split = !sent && wire && wire->length <= 1200 && withAck &&
    head.length && head.length < original.length && !head.fin &&
    head.range.storage == backing && link->ackPending();
  ZuCHECK(split,
    "failed first retransmission must fit the new MTU with sparse ACKs, keep "
    "the original backing, and retain the pending ACK");
  if (!split) { link->teardown(); return; }
  ZuCHECK(link->requeueData(head),
    "failed retransmission head was not retained with its queued tail");

  unsigned packets = 0, fins = 0, total = 0;
  bool valid = true;
  Zutcp::PktNumSpace::T level;
  Zutcp::SentFrameRef ref;
  while (packets < 8 && link->nextRetransmitRef(level, ref)) {
    wire = nullptr;
    bool ok = level == Zutcp::PktNumSpace::AppData &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      link->dataRetx(ref, wire, false, withAck);
    valid &= ok && wire && wire->length <= 1200 && ref.range.storage == backing &&
      ref.offset <= Size && ref.length <= Size - ref.offset &&
      (!ref.fin || ref.offset + ref.length == Size);
    if (!ok || ref.offset > Size || ref.length > Size - ref.offset) break;
    memcpy(decoded + ref.offset, backing->data + ref.range.offset,
      unsigned(ref.length));
    for (unsigned i = unsigned(ref.offset); i < ref.offset + ref.length; ++i)
      ++coverage[i];
    total += unsigned(ref.length);
    fins += ref.fin;
    ++packets;
  }
  bool exact = total == Size && !memcmp(decoded, expected, Size);
  for (unsigned count : coverage) exact &= count == 1;
  ZuCHECK(valid && exact && packets >= 2 && packets < 8 && fins == 1 &&
      !link->ackPending() && cxn->txCreditAvailable() == credit &&
      cxn->txUnackdBytes() == Size + 1,
    "MTU/ACK repacketization must retain every byte exactly once, preserve "
    "flow credit, and carry FIN only at the original tail");
  ZuCHECK(cxn->ackTxUnackd(0, Size, true) && !cxn->txUnackdBytes(),
    "repacketized DATA and FIN did not retire after ACK");
  link->teardown();
#endif
}



void testDataProducerCredit()
{
  ZuTestScope(testDataProducerCredit);

  App app;
  app.deferredTx = true;
  auto link = testLink(&app);
  auto cxn = link;
  unsigned callbacks = 0;
  bool stable = true;
  ZmRef<ZiIOBuf> next = app.dataTxAllocator()->allocBuf();
  next->append(reinterpret_cast<const uint8_t *>("def"), 3);
  Zutcp::DataFrameInfo retained;
  auto packetize = [&cxn](unsigned credit, Zutcp::DataFrameInfo &info) {
    uint8_t out[64];
    Zutcp::PktBudget budget;
    budget.flow = credit;
    Zutcp::PktAssembly assembly;
    return Zutcp::DataPktizer::writeNext(
      out, sizeof(out), budget, assembly, *cxn, &info);
  };
  app.onTx([&app, &cxn, &callbacks, &stable, &next, &retained, &packetize]() {
    cxn->txCredit(6);
    cxn->dequeued = [&app, &cxn, &callbacks, &stable, &next]() {
      ++callbacks;
      stable &= app.mx()->invoked(app.txThread()) &&
	!cxn->txBufferedBytes() && !cxn->txQueueCount() &&
	cxn->txCreditAvailable() == (callbacks == 1 ? 3 : 0) &&
	cxn->txUnackdBytes() == (callbacks == 1 ? 3 : 7) &&
	cxn->finDequeued() == (callbacks == 2);
      if (callbacks == 1) {
	auto tx = cxn->writer_();
	stable &= tx.appendBuf(ZuMv(next));
	cxn->fin();
      }
    };
    auto tx = cxn->writer_();
    tx << "abc" << Zi::flush();
    stable &= packetize(0, retained) < 0 && !callbacks &&
      cxn->txBufferedBytes() == 3;
    stable &= packetize(2, retained) > 0 && retained.length == 2 &&
      !callbacks && cxn->txBufferedBytes() == 1;
    stable &= cxn->recordTxUnackd(retained.offset, retained.length, false);
    stable &= packetize(1, retained) > 0 && retained.length == 1 &&
      !callbacks && !cxn->txBufferedBytes();
    stable &= cxn->recordTxUnackd(retained.offset, retained.length, false);
  });
  app.onTx([&cxn, &callbacks, &stable, &retained, &packetize]() {
    stable &= callbacks == 1 && cxn->txBufferedBytes() == 3 &&
      cxn->txUnackdBytes() == 3;
    stable &= packetize(3, retained) > 0 && retained.fin &&
      retained.length == 3 && callbacks == 1;
    stable &= cxn->recordTxUnackd(retained.offset, retained.length, true);
  });
  app.onTx([&cxn, &callbacks, &stable, &retained, &packetize]() {
    stable &= callbacks == 2 && retained.range.storage &&
      !memcmp(retained.range.storage->data + retained.range.offset, "def", 3);
    stable &= cxn->ackTxUnackd(0, 6, true) &&
      !cxn->ackTxUnackd(0, 6, true) && callbacks == 2;
    Zutcp::DataFrameInfo empty;
    stable &= packetize(3, empty) == 0;
    cxn->dequeued = {};
  });
  link->teardown();
  ZuCHECK(stable && callbacks == 2,
    "producer credit must follow full packetization on AppTx, retain recovery "
    "bytes, allow bounded refill, and leave ACK/FIN idempotent");

  auto canceled = testLink(&app);
  auto pending = canceled;
  app.onTx([&pending, &callbacks, &stable]() {
    pending->txCredit(3);
    pending->dequeued = [&callbacks]() { ++callbacks; };
    auto tx = pending->writer_();
    tx << "abc" << Zi::flush();
    Zutcp::TxRange range;
    stable &= pending->dequeueTxRange(range);
    pending->reset(1);
  });
  app.onTx([&pending]() {
    pending->dequeued = {};
  });
  canceled->teardown();
  ZuCHECK(stable && callbacks == 2,
    "a canceled connection must suppress already queued producer credit");
}

void testDataPktizer()
{
  ZuTestScope(testDataPktizer);

  uint8_t prefix[32];
  uint8_t assembled[64];
  ZuBSpan prefixPayload{"prefix-payload"};
  int prefixLen = Zutcp::FrameCodec::writeDataPrefix(
    prefix, sizeof(prefix), 9, prefixPayload.length(), true);
  ZuCHECK(prefixLen > 0 &&
      unsigned(prefixLen) + prefixPayload.length() <= sizeof(assembled),
    "DATA prefix writer failed");
  memcpy(assembled, prefix, unsigned(prefixLen));
  memcpy(assembled + prefixLen, prefixPayload.data(), prefixPayload.length());
  Zutcp::Frame prefixFrame;
  unsigned prefixUsed = 0;
  ZuCHECK(!Zutcp::FrameCodec::parse(
      ZuBSpan{assembled, unsigned(prefixLen) + prefixPayload.length()},
      prefixFrame, prefixUsed) &&
      prefixUsed == unsigned(prefixLen) + prefixPayload.length() &&
      prefixFrame.type == Zutcp::FrameType::Data &&
      prefixFrame.offset == 9 &&
      prefixFrame.length == prefixPayload.length() &&
      prefixFrame.payload == prefixPayload &&
      prefixFrame.fin,
    "DATA prefix-only assembly parse mismatch");

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto cxn = client;
  cxn->txCredit(6);
  {
    auto tx = cxn->writer_();
    tx << "abc" << Zi::flush();
    tx << "def" << Zi::flush();
  }
  cxn->fin();

  uint8_t b[128];
  Zutcp::PktBudget budget;
  Zutcp::PktAssembly assembly;
  Zutcp::DataFrameInfo info;
  int n = Zutcp::DataPktizer::writeNext(
    b, sizeof(b), budget, assembly, *cxn, &info);
  ZuCHECK(n > 0 &&
      assembly.dataAdded() &&
      budget.used == info.bytes &&
      info.offset == 0 &&
      info.length == 3 &&
      info.bytes == unsigned(n) + info.length &&
      info.range.length == 3 &&
      !memcmp(info.range.storage->data + info.range.offset, "abc", 3) &&
	  !info.fin &&
	  cxn->txRangeCount() == 1,
	"first packetized DATA accounting mismatch");
  unsigned usedAfterFirst = budget.used;

  Zutcp::Frame frame;
  unsigned used = 0;
  memcpy(assembled, b, unsigned(n));
  memcpy(assembled + n, info.range.storage->data + info.range.offset,
    info.range.length);
  ZuCHECK(!Zutcp::FrameCodec::parse(
      ZuBSpan{assembled, info.bytes},
      frame, used) &&
      used == info.bytes &&
      frame.type == Zutcp::FrameType::Data &&
      frame.offset == 0 &&
      frame.length == 3 &&
      frame.payload == "abc" &&
      !frame.fin,
    "first packetized DATA parse mismatch");
  n = Zutcp::DataPktizer::writeNext(
    b, sizeof(b), budget, assembly, *cxn, &info);
  ZuCHECK(n > 0 &&
      budget.used == usedAfterFirst + info.bytes &&
      info.offset == 3 &&
      info.length == 3 &&
	      info.bytes == unsigned(n) + info.length &&
	      info.range.length == 3 &&
	      !memcmp(info.range.storage->data + info.range.offset, "def", 3) &&
	      info.fin &&
	      !cxn->txRangeCount() &&
	      cxn->finDequeued() &&
	      !cxn->finReady(),
	    "second packetized DATA accounting mismatch");
  memcpy(assembled, b, unsigned(n));
  memcpy(assembled + n, info.range.storage->data + info.range.offset,
    info.range.length);
  ZuCHECK(!Zutcp::FrameCodec::parse(
      ZuBSpan{assembled, info.bytes},
      frame, used) &&
      used == info.bytes &&
	      frame.offset == 3 &&
	      frame.length == 3 &&
	      frame.payload == "def" &&
      frame.fin,
    "second packetized DATA parse mismatch");

  budget = {};
	  assembly = {};
	  ZuCHECK(!Zutcp::DataPktizer::writeNext(
	      b, sizeof(b), budget, assembly, *cxn, &info),
	    "packetizer wrote a frame for an empty connection");

  auto blocked = testLink(client->app(), client->isServer());
  blocked->txCredit(7);
  {
    auto tx = blocked->writer_();
    tx << "blocked" << Zi::flush();
  }
  budget = {};
  assembly = {};
  uint8_t tiny[1];
  ZuCHECK(Zutcp::DataPktizer::writeNext(
      tiny, sizeof(tiny), budget, assembly, *blocked, &info) < 0 &&
      blocked->txBufferedBytes() == 7 && !assembly.dataAdded(),
    "prefix capacity must bound output without consuming queued bytes");
  budget = {};
  budget.pmtu = budget.congestion = budget.antiAmplification = 2;
  assembly = {};
  ZuCHECK(Zutcp::DataPktizer::writeNext(
      b, sizeof(b), budget, assembly, *blocked, &info) < 0 &&
      blocked->txRangeCount() == 1 &&
      blocked->txBufferedBytes() == 7 &&
      !assembly.dataAdded(),
    "packetizer consumed data without packet budget");

  auto split = testLink(client->app(), client->isServer());
  split->txCredit(10);
  {
    auto tx = split->writer_();
    tx << "abcdefghij" << Zi::flush();
  }
  budget = {};
  budget.pmtu = budget.congestion = budget.antiAmplification = 8;
  assembly = {};
  n = Zutcp::DataPktizer::writeNext(
    b, sizeof(b), budget, assembly, *split, &info);
  ZuCHECK(n > 0 &&
      unsigned(n) < info.bytes &&
      info.offset == 0 &&
      info.length == 6 &&
      info.bytes == 8 &&
      info.range.length == 6 &&
      !memcmp(info.range.storage->data + info.range.offset, "abcdef", 6) &&
      split->txRangeCount() == 1 &&
      split->txBufferedBytes() == 4,
    "packetizer did not split oversized data range");
  memcpy(assembled, b, unsigned(n));
  memcpy(assembled + n, info.range.storage->data + info.range.offset,
    info.range.length);
  ZuCHECK(!Zutcp::FrameCodec::parse(
      ZuBSpan{assembled, info.bytes},
      frame, used) &&
      used == info.bytes &&
      frame.offset == 0 &&
      frame.length == 6 &&
      frame.payload == "abcdef",
    "split packetized DATA parse mismatch");
  Zutcp::TxRange tail;
  ZuCHECK(split->txRange(0, tail) &&
      tail.length == 4 &&
      tail.dataOffset == 6 &&
      !memcmp(tail.storage->data + tail.offset, "ghij", 4),
	    "split packetizer retained tail mismatch");
  auto gathered = testLink(&app);
  gathered->txCredit(100);
  uint8_t payload[100];
  memset(payload, 'z', sizeof(payload));
  { auto writer = gathered->writer_(); writer << span_(payload, sizeof(payload)) << Zi::flush(); }
  uint8_t header[4];
  budget = {};
  assembly = {};
  n = Zutcp::DataPktizer::writeNext(
    header, sizeof(header), budget, assembly, *gathered, &info);
  ZuCHECK(n == 3 && info.length == sizeof(payload) &&
    info.range.length == sizeof(payload) && info.range.storage &&
    !memcmp(info.range.storage->data + info.range.offset, payload, sizeof(payload)),
    "a bounded prefix scratch span must not cap the separately gathered payload");

}

void testCtlSendFailRetainsFrame()
{
  ZuTestScope(testCtlSendFailRetainsFrame);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  ZuCHECK(client->queueMaxData(8192) &&
      client->queuedControlFrames() == 1,
    "queued control setup failed");
  ZuCHECK(!client->flushControlSendFails() &&
      client->queuedControlFrames() == 1,
    "failed control send dropped queued frame");
  ZuCHECK(client->flushControlSends() &&
      !client->queuedControlFrames(),
    "successful control send did not dequeue frame");
}

void testMultiFrameAssembly()
{
  ZuTestScope(testMultiFrameAssembly);

  App app;
  ZmRef<TestLink> controls = testLink(&app);
  ZuCHECK(controls->queueMaxData(8192), "MAX_DATA assembly setup failed");
  controls->stop(8);
  ZuCHECK(controls->queuedControlFrames() == 2, "multi-control setup failed");
  Zutcp::SentFrameKind::T kinds[8]{};
  uint64_t offsets[8]{};
  unsigned refs = controls->flushFrameRefs(kinds, offsets, 8);
  ZuCHECK(refs == 2 &&
      kinds[0] == Zutcp::SentFrameKind::Control &&
      kinds[1] == Zutcp::SentFrameKind::Control &&
      !controls->queuedControlFrames(),
    "runtime did not assemble multiple control frames into one packet");

  ZmRef<TestLink> queued = testLink(&app);
  queued->grantDataCredit(20000);
  {
    auto tx = queued->writer_();
    tx << "alpha" << Zi::flush();
  }
  {
    auto tx = queued->writer_();
    tx << "bravo" << Zi::flush();
  }
  if (!queued->dataReadyForTest()) {
    queued->scheduleDataForTest();
    queued->scheduleDataForTest();
  }
  refs = queued->flushFrameRefs(kinds, offsets, 8);
  ZuCHECK(refs == 2 &&
      kinds[0] == Zutcp::SentFrameKind::Data &&
      kinds[1] == Zutcp::SentFrameKind::Data &&
      offsets[0] == 0 && offsets[1] == 5,
    "runtime did not assemble multiple data frames into one packet");

  ZmRef<TestLink> finOnly = testLink(&app);
  auto finData = finOnly;
  finOnly->grantDataCredit(20000);
  finData->txCredit(20000);
  {
    auto tx = finData->writer_();
    tx << "payload" << Zi::flush();
  }
  Zutcp::SentFrameRef sentRefs[8];
  refs = finOnly->flushSentRefs(sentRefs, 8);
  ZuCHECK(refs == 1 && sentRefs[0].kind == Zutcp::SentFrameKind::Data &&
      sentRefs[0].length == 7 && !sentRefs[0].fin,
    "runtime did not send first connection data frame");
  finData->fin();
  refs = finOnly->flushSentRefs(sentRefs, 8);
  ZuCHECK(refs == 1 && sentRefs[0].kind == Zutcp::SentFrameKind::Data &&
      sentRefs[0].offset == 7 && !sentRefs[0].length &&
      sentRefs[0].fin,
    "runtime lost FIN-only connection offset");
}

void cumulativeAckRetxSuffix()
{
  ZuTestScope(cumulativeAckRetxSuffix);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto cxn = link;
  link->sendDataRef(0, 0, 10);
  link->sendDataRef(1, 2, 5);
  link->sendAckEliciting(4);
  link->ackBytes(7, 1);
  link->ackOnly(4);
  link->reclaimData(7);

  Zutcp::PktNumSpace::T level = Zutcp::PktNumSpace::Initial;
  Zutcp::SentFrameRef ref;
  ZuCHECK(link->nextRetransmitRef(level, ref) &&
      level == Zutcp::PktNumSpace::AppData &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      ref.offset == 7 && ref.length == 3 && !ref.fin,
    "cumulative byte ACK leaves only the unacknowledged logical suffix");
  ZuCHECK(!link->nextRetransmitRef(level, ref),
    "cumulative retirement must not recreate an earlier packet-ACK hole");
  link->teardown();
}

void testDataRetxClipsUnackdFin()
{
  ZuTestScope(testDataRetxClipsUnackdFin);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto cxn = link;
  link->sendDataRef(0, 0, 10, true);
  ZuCHECK(cxn->ackTxUnackd(0, 10, false) &&
      cxn->txStillUnackd(10, 0, true),
    "connection unackd data ACK did not retain FIN before retransmit");

  link->sendAckEliciting(4);
  link->ackOnly(4);
  link->reclaimData();

  Zutcp::PktNumSpace::T level = Zutcp::PktNumSpace::Initial;
  Zutcp::SentFrameRef ref;
  ZuCHECK(link->nextRetransmitRef(level, ref) &&
      level == Zutcp::PktNumSpace::AppData &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      ref.offset == 10 &&
      !ref.length &&
      ref.fin,
    "stream retransmit did not clip ACKd data before retained FIN");
  ZuCHECK(!link->nextRetransmitRef(level, ref),
    "stream FIN clipping left extra retransmit work");
  link->teardown();
}

void testPktNumLength()
{
  ZuTestScope(testPktNumLength);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  ZuCHECK(link->pnLength() == 2,
    "runtime PN length default before ACK changed");
  link->advancePN(10);
  link->ackThrough(9);
  ZuCHECK(link->pnLength() == 1,
    "runtime PN length did not shrink after near ACK");
  link->advancePN(200);
  ZuCHECK(link->pnLength() == 2,
    "runtime PN length did not grow to two bytes");
#ifdef Zutcp_DEBUG
  link->forcePN(40000);
  ZuCHECK(link->pnLength() == 3,
    "runtime PN length did not grow to three bytes");
  link->forcePN(9000000);
  ZuCHECK(link->pnLength() == 4,
    "runtime PN length did not grow to four bytes");
#endif
  link->closeForTest();
}

static void ackCreditServicesRetx(bool pending)
{
  ZuTestScopeRT(ackCreditServicesRetx);

  App app;
  app.deferredTx = true;
  auto link = testLink(&app);
  auto cxn = link;
  auto retained = app.dataTxAllocator()->allocBuf();
  retained->append(reinterpret_cast<const uint8_t *>("retained"), 8);
  RecoveryService service;
  service.origin = retained;
  bool setup = false, kept = false, released = false;
  app.onTx([&link, &cxn, &retained, &service, &setup, pending]() {
    link->grantDataCredit(8);
    cxn->txCredit(8);
    {
      auto out = cxn->writer_();
      out.appendBuf(retained);
      out.flush();
    }
    // Record real retained DATA references. The cumulative RTO history path
    // selects DATA loss; packet ACKs only reopen the control flight credit.
    bool sent = link->flushCongestedData();
    if (pending) {
      link->sendAckEliciting(4, 100);
      link->ackOnly(4);
      link->reclaimData();
      link->sentPkts = 5;
    }
    link->fillCwnd();
    setup = sent && cxn->txStillUnackd(0, 8) &&
      link->diag().tx.retransmitPending[Zutcp::PktNumSpace::AppData] ==
        unsigned(pending) && !link->congestionAllowance();
    link->service = &service;
    if (pending) {
      // Open credit without newly detecting loss: pre-existing pending
      // recovery must still be serviced on the ACK owner.
      link->ackOnly(5);
    } else {
      // Also exercise the newly-lost notification. Its real deferred owner
      // task must not lose all credit to a synchronous ordinary flush.
      link->reclaimData();
      link->ackRange(3, link->sentPkts - 1);
    }
  });
  app.drainTx(); // includes any genuinely deferred recovery notification
  app.onTx([&link, &cxn, &service, &kept, &released]() {
    kept = cxn->txStillUnackd(0, 8) && service.valid;
    link->service = nullptr;
    if (service.recovered) link->ackBytes(8, service.recoveredPN);
    released = service.recovered && !cxn->txStillUnackd(0, 8);
  });
  link->teardown();
  ZUTCP_CHECK_RT(setup, "retained DATA and closed congestion setup failed");
  ZUTCP_CHECK_RT(service.recovered == 1 && service.events >= 2 &&
      service.order[0] == 'R' && service.order[1] == 'N',
    "ACK-reopened credit went to bulk before already-pending recovery");
  ZUTCP_CHECK_RT(!service.blocked && !service.queued,
    "recovery was deferred behind the synchronous bulk flush");
  ZUTCP_CHECK_RT(kept && released,
    "retained DATA backing/ACK ownership changed while servicing recovery");
}

void congestionBudgetGatesSends()
{
  ZuTestScope(congestionBudgetGatesSends);

#ifdef Zutcp_DEBUG
  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto cxn = link;
  link->grantDataCredit(20000);
  cxn->txCredit(20000);
  {
    auto tx = cxn->writer_();
    tx << "0123456789abcdef0123456789abcdef" << Zi::flush();
  }
  cxn->fin();

  link->fillCwnd();
  Zutcp::LinkDiag diag = link->diag();
  ZuCHECK(diag.tx.bytesInFlight >= diag.tx.congestionWindow &&
      !link->flushCongestedData() &&
      cxn->txBufferedBytes(),
    "runtime stream send bypassed closed congestion window");

  ZuCHECK(!link->congestionAllowance(),
    "runtime congestion allowance remained open after cwnd fill");
  ZuCHECK(link->queueMaxData(8192), "runtime control frame queue failed");
  ZuCHECK(!link->flushControlSends(),
    "runtime control send bypassed closed congestion window");
  ZuCHECK(link->queuedControlFrames() == 1,
    "runtime blocked control frame was not retained");

  unsigned queued = link->txFlushQueued;
  link->ackThrough(link->sentPkts - 1);
  diag = link->diag();
  ZuCHECK(diag.tx.bytesInFlight < diag.tx.congestionWindow &&
      link->txFlushQueued == queued + 1,
    "runtime ACK did not reopen congestion window and queue Tx flush");
  bool flushed = link->flushControlSends();
  ZuCHECK(flushed, "runtime control send did not resume after ACK opened cwnd");
  ZuCHECK(!link->queuedControlFrames(),
    "runtime control queue did not drain after ACK opened cwnd");
  link->closeForTest();
#endif
}

static void testDataBatchContinuation()
{
  ZuTestScope(testDataBatchContinuation);

#ifdef Zutcp_DEBUG
  App app;
  ZmRef<TestLink> link = testLink(&app);
  using Data = ZtArray<uint8_t, ZtArrayHeapID<"Zutcp.Test.DataBatch">>;
  Data data;
  data.length((TestLink::TxFlushBatch + 1) * Zutcp::BufSize);
  memset(data.data(), 'x', data.length());
  // Grow the real controller through ACKs until a complete flush batch fits
  // without waiting for more network input. Each round doubles slow start.
  while (link->diag().tx.congestionWindow < (data.length()<<1)) {
    uint64_t before = link->diag().tx.congestionWindow;
    link->fillCwnd();
    link->ackThrough(link->sentPkts - 1);
    if (link->diag().tx.congestionWindow <= before) break;
  }
  bool allowance = link->congestionAllowance() >= data.length();
  ZuCHECK(allowance, "batch continuation setup did not open enough congestion credit");
  if (!allowance) {
    link->teardown();
    return;
  }
  auto cxn = link;
  link->grantDataCredit(data.length());
  cxn->txCredit(data.length());
  {
    auto tx = cxn->writer_();
    tx << data;
    tx.flush();
  }
  cxn->fin();
  unsigned queued = link->txFlushQueued;
  uint64_t before = link->sentPkts;
  ZuCHECK(link->flushDataBatch() &&
      link->sentPkts == before + TestLink::TxFlushBatch &&
      link->dataReadyForTest() && cxn->txBufferedBytes() &&
      link->txFlushQueued == queued + 1,
    "productive full batch must yield and schedule only its own remaining output");
  queued = link->txFlushQueued;
  ZuCHECK(link->flushDataBatch() && !link->dataReadyForTest() &&
      !cxn->txBufferedBytes() && link->txFlushQueued == queued,
    "final partial batch must drain without another continuation");
  link->teardown();

  for (bool congestion : {false, true}) {
    ZmRef<TestLink> blocked = testLink(&app);
    auto pending = blocked;
    blocked->grantDataCredit(100);
    pending->txCredit(congestion ? 100 : 0);
    {
      auto tx = pending->writer_();
      tx << "blocked";
      tx.flush();
    }
    if (congestion) blocked->fillCwnd();
    // A flow-blocked producer may emit its one DATA_DATA_BLOCKED notice.
    // Once that notice is sent, only new credit can make further progress.
    else (void)blocked->flushDataBatch();
    queued = blocked->txFlushQueued;
    before = blocked->sentPkts;
    (void)blocked->flushDataBatch();
    ZuCHECK(blocked->sentPkts == before && pending->txBufferedBytes() &&
        blocked->txFlushQueued == queued,
      "credit-blocked batch must await its producer instead of scheduling a spin");
    blocked->teardown();
  }
#endif
}

void testActivePathBudget()
{
  ZuTestScope(testActivePathBudget);

#ifdef Zutcp_DEBUG
  {
    App app{1360};
    ZmRef<TestLink> client = testLink(&app);
    client->growActivePath(1360);
    Zutcp::PktBudget budget = client->sendBudget();
    ZuCHECK(client->pathValidated() &&
	client->activePathMaxPayload() == 1360 &&
	budget.pmtu == 1360 - Zutcp::TxDataPktReserve &&
	budget.limit() == 1360 - Zutcp::TxDataPktReserve,
      "active path max UDP did not drive runtime packet budget");
    client->closeForTest();
  }
  Zi::Path path = testPath("ZutcpDataPathBudgetQLog.sqlog");
  ZiFile::remove(path);
	ZutcpLogParams params;
	params.enabled(true).path(path).
	  ringSize(1<<15);
#endif

	App app;
#ifdef Zutcp_DEBUG
	ZuCHECK(ZutcpLogger::init(
	    app.qlogTrace(), params, Zutcp::Vantage::Unknown),
	  "path-budget qlog init failed");
	ZutcpLogger::start();
#endif
	ZmRef<TestLink> server = testLink(&app, true);
  server->initServerPath();
  ZuCHECK(!server->pathValidated() &&
      !server->pathAntiAmp() &&
      !server->pathSend(1),
    "unvalidated server path sent without received bytes");

  server->pathReceived(400);
  ZuCHECK(server->pathAntiAmp() == 1200 &&
      server->pathSend(1000) &&
      !server->pathSend(201) &&
      server->pathSend(200) &&
      !server->pathAntiAmp(),
    "unvalidated server path did not enforce 3x send budget");

  Zutcp::PathDiag diag = server->pathDiag();
  ZuCHECK(diag.bytesRx == 400 && diag.bytesTx == 1200,
    "active path byte accounting mismatch after budget exhaustion");

  server->pathReceived(100);
  ZuCHECK(server->pathAntiAmp() == 300 &&
      server->pathSend(300),
    "received bytes did not expand server anti-amplification budget");

  server->validatePath();
  ZuCHECK(server->pathValidated() &&
      ZuCmp<uint64_t>::null(server->pathAntiAmp()) &&
      server->pathSend(Zutcp::MinPktPayload),
    "address validation did not unlock active path send allowance");
  server->closeForTest();

#ifdef Zutcp_DEBUG
	closeQLog(app.qlogTrace());
	ZutcpLogger::stop();
	ZutcpLogDiag qdiag = ZutcpLogger::diag();
	ZutcpLogger::final(app.qlogTrace());
  ZuCHECK(qdiag.recordsEnqueued >= 4, "path-budget qlog enqueue mismatch");
  ZuCHECK(qdiag.recordsWritten >= 5, "path-budget qlog write mismatch");
  ZuCHECK(qdiag.writerFailures == 0, "path-budget qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "path-budget qlog output was not written");
  ZuCHECK(parseJSONSeq(data) >= 5,
    "path-budget qlog JSON-SEQ parse failed");
	  ZuCHECK(data.find<"quic:packet_dropped">() >= 0,
	    "path-budget qlog missing packet_dropped");
	  ZuCHECK(data.find<"\"trigger\":\"rejected\"">() >= 0,
	    "path-budget qlog missing rejected trigger");
  ZiFile::remove(path);
#endif
}





void testBootstrapCIDRoutes()
{
  ZuTestScope(testBootstrapCIDRoutes);
#ifdef Zutcp_DEBUG
  App app;
  auto link = testLink(&app, true);
  ZuCHECK(link->establish(Zutcp::CxnID{"localcid"}), "bootstrap route setup failed");
  TestCIDRoutes routes;
  link->installLocalCIDRoutes(routes);
  link->installLocalCIDRoutes(routes);
  ZuCHECK(routes.adds == 1, "repeated packets reinstalled the fixed local route");
  link->tombstoneLocalCIDRoutes(routes);
  link->tombstoneLocalCIDRoutes(routes);
  ZuCHECK(routes.tombstones == 1, "fixed local route was not tombstoned once");
  link->teardown();
#endif
}

void testPMTUDQLog()
{
  ZuTestScope(testPMTUDQLog);

#ifdef Zutcp_DEBUG
  Zi::Path path = testPath("ZutcpDataPMTUDQLog.sqlog");
	ZiFile::remove(path);
	ZutcpLogParams params;
	params.enabled(true).path(path).ringSize(1<<15);

	App app{Zutcp::BufSize};
	ZuCHECK(ZutcpLogger::init(
	    app.qlogTrace(), params, Zutcp::Vantage::Unknown),
	  "PMTUD qlog init failed");
	ZutcpLogger::start();
	ZmRef<TestLink> link = testLink(&app, true);
  link->initServerPath(
    ZiSockAddr{ip4_(0x0a000001), 4433},
    ZiSockAddr{ip4_(0x0a000002), 50000});
  link->validatePath();
  ZuCHECK(link->pathValidated(), "PMTUD qlog path validation setup failed");

  ZuCHECK(link->startPMTUDProbe(1400),
    "PMTUD qlog ACK probe setup failed");
  link->ackPMTUDProbe(1400);
  ZuCHECK(link->activePathMaxPayload() == 1400,
    "PMTUD qlog ACK did not grow active path");

  ZuCHECK(link->startPMTUDProbe(1500),
    "PMTUD qlog loss probe setup failed");
  link->losePMTUDProbe(1500);
  link->closeForTest();

	closeQLog(app.qlogTrace());
	ZutcpLogger::stop();
	ZutcpLogDiag diag = ZutcpLogger::diag();
	ZutcpLogger::final(app.qlogTrace());
  ZuCHECK(diag.recordsEnqueued >= 2, "PMTUD qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 3, "PMTUD qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "PMTUD qlog writer failure");

  ZtString<> qlog = readFile_(path);
  ZuCHECK(qlog, "PMTUD qlog output missing");
  ZuCHECK(parseJSONSeq(qlog) >= 3, "PMTUD qlog JSON-SEQ parse failed");
  ZuCHECK(qlog.find<"quic:mtu_updated">() >= 0,
    "PMTUD qlog missing mtu_updated");
  ZuCHECK(qlog.find<"\"new\":1400">() >= 0,
    "PMTUD qlog missing ACKed MTU");
  ZuCHECK(qlog.find<"\"new\":1500">() >= 0,
    "PMTUD qlog missing lost probe MTU");
  ZuCHECK(qlog.find<"\"done\":true">() >= 0,
    "PMTUD qlog missing completion flag");
#endif
}

#ifdef Zutcp_DEBUG


void testAckECNValidateDisable()
{
  ZuTestScope(testAckECNValidateDisable);

  Zi::Path path = testPath("ZutcpDataECNQLog.sqlog");
	ZiFile::remove(path);
	ZutcpLogParams params;
	params.enabled(true).path(path).ringSize(1<<15);

	App app;
	ZuCHECK(ZutcpLogger::init(
	    app.qlogTrace(), params, Zutcp::Vantage::Unknown),
	  "ECN qlog init failed");
	ZutcpLogger::start();
	ZmRef<TestLink> link = testLink(&app);
  link->enableECN();
  ZuCHECK(!link->ecnDisabled(), "ECN did not enable for validation test");
  ZuCHECK(link->receiveMarked(1, Zutcp::EcnMark::ECT0) &&
      link->receiveMarked(2, Zutcp::EcnMark::CE),
    "runtime ECN receive marking failed");
  Zutcp::LinkDiag diag = link->diag();
  ZuCHECK(diag.rx.ecnRx[Zutcp::PktNumSpace::AppData].ect0 == 1 &&
      diag.rx.ecnRx[Zutcp::PktNumSpace::AppData].ce == 1,
    "runtime ECN receive diagnostics mismatch");
  Zutcp::PktBuild build;
  ZuCHECK(link->writePendingAck(build) &&
      build.count() == 1 &&
      build.data()[0].length() > 0,
    "runtime pending ACK_ECN build failed");
  Zutcp::Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zutcp::FrameCodec::parse(
      ZuBSpan{build.data()[0].data(), unsigned(build.data()[0].length())},
      frame, used) &&
      frame.type == Zutcp::FrameType::Ack &&
      frame.ackECN.ect0 == 1 &&
      frame.ackECN.ce == 1,
    "runtime received ECN marks were not emitted in ACK_ECN");

  link->sendECN(0, Zutcp::EcnMark::ECT0);
  link->sendECN(1, Zutcp::EcnMark::ECT0);
  link->sendECN(2, Zutcp::EcnMark::ECT1);
  link->ackECN(2, 2, 1, 0);
  diag = link->diag();
  ZuCHECK(!link->ecnDisabled() &&
      diag.tx.peerAckECN[Zutcp::PktNumSpace::AppData].ect0 == 2 &&
      diag.tx.peerAckECN[Zutcp::PktNumSpace::AppData].ect1 == 1,
    "valid ACK_ECN did not update runtime diagnostics");
  link->ackECN(2, 1, 1, 0);
  diag = link->diag();
  ZuCHECK(link->ecnDisabled() && diag.tx.ecnValidationFailures == 1,
    "regressing ACK_ECN did not disable ECN");
  link->closeForTest();

  ZmRef<TestLink> missing = testLink(&app);
  missing->enableECN();
  for (unsigned i = 0; i < Zutcp::Path::ECNProbeThreshold - 1; ++i) {
    missing->sendECN(i, Zutcp::EcnMark::ECT0);
    missing->ackNoECN(i);
  }
  diag = missing->diag();
  ZuCHECK(!missing->ecnDisabled() && diag.tx.ecnValidationFailures == 0,
    "missing ACK_ECN failed before probe budget expired");
  missing->sendECN(
    Zutcp::Path::ECNProbeThreshold - 1, Zutcp::EcnMark::ECT0);
  missing->ackNoECN(Zutcp::Path::ECNProbeThreshold - 1);
  diag = missing->diag();
  ZuCHECK(missing->ecnDisabled() && diag.tx.ecnValidationFailures == 1,
    "missing ACK_ECN did not fail after probe budget expired");
  missing->closeForTest();

  ZmRef<TestLink> impossible = testLink(&app);
  impossible->enableECN();
  impossible->sendAckEliciting(0);
  diag = impossible->diag();
  uint64_t inFlight = diag.tx.bytesInFlight;
  unsigned queued = impossible->txFlushQueued;
  impossible->ackECN(0, 2, 0, 0);
  diag = impossible->diag();
  ZuCHECK(impossible->ecnDisabled() &&
      diag.tx.ecnValidationFailures == 1 &&
      diag.tx.bytesInFlight < inFlight &&
      impossible->txFlushQueued == queued + 1,
    "impossible ACK_ECN did not disable ECN while preserving ACK processing");
  impossible->closeForTest();

  ZmRef<TestLink> ce = testLink(&app);
  ce->enableECN();
  ce->sendECN(0, Zutcp::EcnMark::ECT0);
  ce->sendECN(1, Zutcp::EcnMark::ECT0);
  diag = ce->diag();
  uint64_t cwnd = diag.tx.congestionWindow;
  ce->ackECN(1, 1, 0, 1);
  diag = ce->diag();
  Zutcp::PathDiag pathDiag = ce->pathDiag();
  ZuCHECK(!ce->ecnDisabled() &&
      diag.tx.peerAckECN[Zutcp::PktNumSpace::AppData].ce == 1 &&
      diag.tx.congestionWindow < cwnd &&
      pathDiag.ecnCEEvents == 1 &&
      pathDiag.ecnCEBytes == 2400,
    "CE ACK_ECN did not drive congestion response and path diagnostics");
  ce->closeForTest();

	closeQLog(app.qlogTrace());
	ZutcpLogger::stop();
	ZutcpLogDiag qdiag = ZutcpLogger::diag();
	ZutcpLogger::final(app.qlogTrace());

  ZuCHECK(qdiag.recordsEnqueued >= 3, "ECN qlog enqueue mismatch");
  ZuCHECK(qdiag.recordsWritten >= 3, "ECN qlog write mismatch");
  ZuCHECK(qdiag.writerFailures == 0, "ECN qlog writer failure");

  ZtString<> qlog = readFile_(path);
  ZuCHECK(qlog, "ECN qlog output missing");
  ZuCHECK(parseJSONSeq(qlog) >= 4, "ECN qlog JSON-SEQ parse failed");
  ZuCHECK(qlog.find<"quic:ecn_state_updated">() >= 0,
    "ECN qlog event missing");
  ZuCHECK(qlog.find<"\"old\":\"unknown\"">() >= 0 &&
      qlog.find<"\"new\":\"testing\"">() >= 0,
    "ECN qlog missing testing state");
  ZuCHECK(qlog.find<"\"old\":\"testing\"">() >= 0 &&
      qlog.find<"\"new\":\"capable\"">() >= 0,
    "ECN qlog missing capable state");
  ZuCHECK(qlog.find<"\"old\":\"testing\"">() >= 0 &&
      qlog.find<"\"new\":\"failed\"">() >= 0,
    "ECN qlog missing failed state");
  ZuCHECK(qlog.find<"counter_exceeds_ack">() < 0,
    "ECN qlog leaked private validation reason");
}
#endif

void testBlockedFrameDedup()
{
  ZuTestScope(testBlockedFrameDedup);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  ZuCHECK(client->queueDataBlocked(1024) &&
      !client->queueDataBlocked(1024) &&
      client->queuedControlFrames() == 1,
    "pending DATA_BLOCKED duplicate was queued");
  (void)client->flushControlSends();
  ZuCHECK(
      !client->queuedControlFrames() &&
      !client->queueDataBlocked(1024) &&
      client->queueDataBlocked(2048) &&
      client->queuedControlFrames() == 1,
    "sent DATA_BLOCKED duplicate suppression mismatch");

}

void testKeyedControlReplacement()
{
  ZuTestScopeRT(testKeyedControlReplacement);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  ZUTCP_CHECK_RT(link->queueMaxData(1024) &&
      !link->queueMaxData(1024) &&
      link->queueMaxData(2048) &&
      link->queuedControlFrames() == 1,
    "MAX_DATA keyed replacement mismatch");


}

void testControlInvalidation()
{
  ZuTestScope(testControlInvalidation);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  ZmRef<TestLink> blocked = testLink(&app);
  ZuCHECK(blocked->queueDataBlocked(1024) &&
      blocked->queuedControlFrames() == 1,
    "DATA_BLOCKED setup failed");
  blocked->grantDataCredit(2048);
  ZuCHECK(!blocked->queuedControlFrames(),
    "DATA_BLOCKED was not cleared when peer credit arrived");
}

void testResetStopFrames()
{
  ZuTestScope(testResetStopFrames);

  App app;
  uint8_t b[128];
  Zutcp::Frame frame;
  unsigned used = 0;

  ZmRef<TestLink> server = testLink(&app, true);
  int n = Zutcp::FrameCodec::writeReset(b, sizeof(b), 7, 0);
  ZuCHECK(n > 0 && !Zutcp::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used),
    "RESET_DATA setup failed");
  ZuCHECK(server->receiveFrame(frame) == 0, "server rejected peer RESET_DATA");
  auto reset = server;
  ZuCHECK(reset &&
      reset->resetReceived() &&
      reset->rxError() == Zutcp::DataError::Reset &&
      reset->rxAppError() == 7 &&
      reset->rxComplete() &&
      !reset->finalSize(),
    "RESET_DATA state mismatch");

  ZmRef<TestLink> client = testLink(&app);
  auto local = client;
  n = Zutcp::FrameCodec::writeStopSending(b, sizeof(b), 9);
  ZuCHECK(n > 0 && !Zutcp::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used),
    "STOP_SENDING setup failed");
  ZuCHECK(client->receiveFrame(frame) == 0 &&
      local->stopReceived() &&
      local->txError() == Zutcp::DataError::Stop &&
      local->txAppError() == 9,
    "STOP_SENDING state mismatch");

  auto delivered = testLink(client->app(), client->isServer());
  auto packet = dataPkt(0, "hello", false, frame, used);
  ZuCHECK(packet &&
      delivered->receiveDataFrame(frame, packet),
    "delivered DATA setup failed");
  n = Zutcp::FrameCodec::writeReset(b, sizeof(b), 1, 3);
  ZuCHECK(n > 0 && !Zutcp::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used),
    "final-size violating RESET_DATA setup failed");
  ZuCHECK(!delivered->receiveReset(frame),
    "RESET_DATA final-size violation was accepted");
}

void testLocalResetStopSend()
{
  ZuTestScope(testLocalResetStopSend);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  Zutcp::SentFrameRef refs[2];

  auto reset = link;
  {
    auto tx = reset->writer_();
    tx << "abc" << Zi::flush();
  }
  reset->reset(42);
  ZuCHECK(reset->resetSent() &&
      reset->txError() == Zutcp::DataError::Reset &&
      reset->txAppError() == 42 &&
      !reset->txRangeCount() && !reset->txBufferedBytes() && !reset->txQueueCount() &&
      link->queuedControlFrames() == 1,
    "local RESET_DATA did not queue control state");
  unsigned n = link->flushSentRefs(refs, 2);
  ZuCHECK(n == 1 &&
      refs[0].kind == Zutcp::SentFrameKind::Control &&
      refs[0].controlType == Zutcp::FrameType::Reset &&
      refs[0].value == 42 &&
      !refs[0].length &&
      !link->queuedControlFrames(),
    "unsent application bytes entered RESET_DATA final size");
  Zutcp::Frame rebuilt;
  ZuCHECK(link->rebuildControl(refs[0], rebuilt) &&
      rebuilt.type == Zutcp::FrameType::Reset &&
      rebuilt.errorCode == 42 &&
      !rebuilt.length,
    "local RESET_DATA retransmit rebuild failed");
  ZuCHECK(!reset->ackReset(42, 3) && reset->ackReset(42, 0),
    "RESET_DATA acknowledgement used queued application extent");

  auto partial = testLink(link->app(), link->isServer());
  partial->txCredit(2);
  {
    auto tx = partial->writer_();
    tx << "abcdef" << Zi::flush();
  }
  n = partial->flushSentRefs(refs, 2);
  ZuCHECK(n == 1 && refs[0].kind == Zutcp::SentFrameKind::Data &&
      refs[0].length == 2 && partial->txBufferedBytes() == 4 &&
      !partial->queuedControlFrames(),
    "partial DATA transmission did not stop at peer credit");
  ZuCHECK(partial->recordTxUnackd(0, 2, false),
    "partial DATA recovery range setup failed");
  auto blockedRef = Zutcp::SentFrameRef::blocked(2);
  ZuCHECK(!partial->rebuildControl(blockedRef, rebuilt),
    "flow-blocked DATA must not enter reliable blocked-control recovery");
  partial->reset(43);
  ZuCHECK(!partial->txRangeCount() && !partial->txBufferedBytes() &&
      !partial->txQueueCount() && !partial->txUnackdCount() &&
      partial->queuedControlFrames() == 1 && !partial->rebuildControl(blockedRef, rebuilt),
    "RESET_DATA retained unsent or recovery ranges");
  n = partial->flushSentRefs(refs, 2);
  ZuCHECK(n == 1 && refs[0].controlType == Zutcp::FrameType::Reset &&
      partial->rebuildControl(refs[0], rebuilt) && rebuilt.length == 2 &&
      !partial->ackReset(43, 6) && partial->ackReset(43, 2),
    "flow-stalled RESET_DATA included unsent bytes in final size");

  auto stop = testLink(link->app(), link->isServer());
  stop->stop(9);
  ZuCHECK(stop->stopSent() &&
      stop->rxError() == Zutcp::DataError::Stop &&
      stop->rxAppError() == 9 &&
      stop->queuedControlFrames() == 1,
    "local STOP_SENDING did not queue control state");
  n = stop->flushSentRefs(refs, 2);
  ZuCHECK(n == 1 &&
      refs[0].kind == Zutcp::SentFrameKind::Control &&
      refs[0].controlType == Zutcp::FrameType::StopSending &&
      refs[0].value == 9,
    "local STOP_SENDING sent ref mismatch");
  ZuCHECK(stop->rebuildControl(refs[0], rebuilt) &&
      rebuilt.type == Zutcp::FrameType::StopSending &&
      rebuilt.errorCode == 9,
    "local STOP_SENDING retransmit rebuild failed");

  stop->sendControlRef(1, refs[0]);
  stop->sendControlRef(2, refs[0]);
  stop->sendAckEliciting(5);
  stop->ackOnly(1);
  stop->ackOnly(5);
  Zutcp::SentFrameRef ref;
  Zutcp::PktNumSpace::T level = Zutcp::PktNumSpace::Initial;
  ZuCHECK(!stop->nextRetransmitRef(level, ref),
    "ACKd STOP_SENDING was retransmitted after later loss");
  stop->teardown();

  ZmRef<TestLink> lost = testLink(&app);
  auto lostStop = lost;
  lostStop->stop(11);
  n = lost->flushSentRefs(refs, 2);
  ZuCHECK(n == 1 &&
      refs[0].kind == Zutcp::SentFrameKind::Control &&
      refs[0].controlType == Zutcp::FrameType::StopSending,
    "lost STOP_SENDING sent ref setup failed");
  lost->sendControlRef(2, refs[0]);
  lost->sendAckEliciting(5);
  lost->ackOnly(5);
  ZuCHECK(lost->nextRetransmitRef(level, ref) &&
      ref.kind == Zutcp::SentFrameKind::Control &&
      ref.controlType == Zutcp::FrameType::StopSending &&
      !lost->nextRetransmitRef(level, ref),
    "lost STOP_SENDING did not queue one retransmit");
  lost->teardown();
}

void tcpReceiveWindow()
{
  ZuTestScopeRT(tcpReceiveWindow);
  App app{Zutcp::MinPktPayload, 8};
  app.requireNativeTx = true;
  auto link = testLink(&app);
  Zutcp::Frame frame;
  unsigned used = 0;
  bool immediate = false;
  auto packet = dataPkt(777, "efghijkl", true, frame, used);
  ZuCHECK(packet && link->receiveTCPFrame(packet,
      {.seq = 4, .flags = 0x11}, immediate) &&
      link->rxPending() == 1 && link->rxDataCreditUsed() == 8 &&
      !link->finReceived(),
    "right-edge DATA must retain only its in-window slice and discard FIN beyond the edge");
  packet = dataPkt(777, "abcd", false, frame, used);
  ZuCHECK(packet && link->receiveTCPFrame(packet, {.seq = 0}, immediate) &&
      link->rxBytes() == 8 && link->rxAdmitted() == 8 && !link->rxPending(),
    "header sequence must close the gap without admitting clipped bytes");
  packet = dataPkt(777, "ijkl", false, frame, used);
  ZuCHECK(packet && link->receiveTCPFrame(packet, {.seq = 8}, immediate) && immediate &&
      link->rxBytes() == 8 && link->rxAdmitted() == 8 && !link->rxCreditAvailable(),
    "zero-window DATA must request a current ACK without mutating receive state");
  ZuCHECK(consumeExact_(link->reader(), 4, "abcd") &&
      consumeExact_(link->reader(), 4, "efgh") && link->retireRx(8),
    "the original right-edge slices must deliver exact bytes before reopening");
  packet = dataPkt(777, "ghijklmnopqr", true, frame, used);
  auto origin = frame.payload.data();
  ZuCHECK(packet && link->receiveTCPFrame(packet,
      {.seq = 6, .flags = 0x11}, immediate) && link->rxBytes() == 16 &&
      link->rxAdmitted() == 16 && !link->finReceived() &&
      link->reader().span().data() == origin + 2 &&
      consumeExact_(link->reader(), 8, "ijklmnop"),
    "both-edge trimming must preserve the original payload address and exact novel bytes");
  packet = dataPkt(777, "", true, frame, used);
  ZuCHECK(packet && link->receiveTCPFrame(packet,
      {.seq = 16, .flags = 0x11}, immediate) && immediate && !link->finReceived(),
    "FIN at a closed right edge must wait for receive space");
  ZuCHECK(link->retireRx(8) && link->receiveTCPFrame(packet,
      {.seq = 16, .flags = 0x11}, immediate) && link->rxComplete() &&
      link->finalSize() == 16,
    "the same FIN must complete after the application reopens the window");
  link->teardown();

  auto prefix = testLink(&app);
  packet = dataPkt(777, "xxabcd", false, frame, used);
  origin = frame.payload.data();
  ZuCHECK(packet && prefix->receiveTCPFrame(packet, {.seq = 0xfffffffe}, immediate) &&
      prefix->rxBytes() == 4 && prefix->rxAdmitted() == 4 &&
      prefix->reader().span().data() == origin + 2 &&
      consumeExact_(prefix->reader(), 4, "abcd"),
    "a duplicate prefix before the relative origin must trim through sequence wrap");
  packet = dataPkt(777, "abcd", false, frame, used);
  ZuCHECK(packet && prefix->receiveTCPFrame(packet, {.seq = 0}, immediate) && immediate &&
      prefix->rxBytes() == 4 && prefix->rxAdmitted() == 4 && !prefix->reader(),
    "wholly duplicate input must only request the current ACK");
  ZuCHECK(prefix->receiveTCPFrame(packet, {.seq = 32}, immediate) && immediate &&
      prefix->rxBytes() == 4 && prefix->rxAdmitted() == 4 && !prefix->rxPending(),
    "wholly future input must not allocate a receive slice or close the connection");
  prefix->teardown();
}

void tcpSegmentAdmission()
{
  ZuTestScopeRT(tcpSegmentAdmission);
  App app{Zutcp::MinPktPayload, 8};
  auto link = testLink(&app);
  Zutcp::CxnID cid{"admitcid"};
  ZuCHECK(link->establish(cid), "native segment fixture must establish");
  link->txCredit(64);
  auto retained = link->prepareDataRef(0, 0, 4);
  link->acceptTxData(retained);
  ZuCHECK(link->consumeTxCredit(4), "segment fixture must account outstanding DATA positions");
  app.requireNativeTx = true;
  auto packet = [cid](uint64_t pn, ZuBSpan frames) -> ZmRef<ZiIOBuf> {
    static ZiMultiplex mx;
    auto buf = ZutcpTest::pktRxAllocator(&mx)->allocBuf();
    if (!buf) return {};
    int n = Zutcp::Pkt::writeShort(buf->data(), buf->avail(), cid, pn, 2);
    if (n < 0 || frames.length() > buf->avail() - unsigned(n)) return {};
    memcpy(buf->data() + n, frames.data(), frames.length());
    buf->length = unsigned(n) + frames.length();
    return buf;
  };
  uint8_t frames[128];
  int ack = Zutcp::FrameCodec::writeAck(frames, sizeof(frames), 0, 0, 0);
  int reset = ack > 0 ? Zutcp::FrameCodec::writeReset(
    frames + ack, sizeof(frames) - ack, 7, 0) : -1;
  ZuCHECK(ack > 0 && reset > 0, "hostile ACK/reset fixture must encode");
  if (ack <= 0 || reset <= 0) { link->teardown(); return; }
  auto flight = link->diag().tx.bytesInFlight;
  Zutcp::TCPHeader hostile{.seq = 8, .ack = 4, .window = 1, .flags = 0x10};
  ZuCHECK(link->receiveShort(packet(11, {frames, unsigned(ack + reset)}), nullptr, hostile) &&
      !link->txAckd() && link->txCreditLimit() == 64 && link->txUnackdBytes() == 4 &&
      !link->resetReceived() && !link->receivedPN(11) && !link->ackPending() &&
      link->diag().tx.bytesInFlight == flight,
    "right-edge control segment must not commit header ACK/window, reset or packet evidence");
  Zutcp::TCPHeader response;
  unsigned frameBytes = ~unsigned(0);
  ZuCHECK(link->windowReply(response, frameBytes) && !frameBytes &&
      !response.ack && response.window == 8 && !link->ackPending() &&
      !link->windowReply(response, frameBytes),
    "refused control must produce current native ACK/window without ACKing its packet number");
  uint8_t ping = 1;
  hostile.seq = 0xffffffff;
  ZuCHECK(link->receiveShort(packet(12, {&ping, 1}), nullptr, hostile) &&
      !link->txAckd() && link->txCreditLimit() == 64 && !link->receivedPN(12),
    "before-origin persist PING must not install its advancing header ACK/window");
  ZuCHECK(link->windowReply(response, frameBytes) && !frameBytes && response.window == 8,
    "unchanged edge must produce another persist reply after successful-send commitment");
  hostile.seq = 0;
  ZuCHECK(link->receiveShort(packet(13, {&ping, 1}), nullptr, hostile) &&
      link->txAckd() == 4 && link->txCreditLimit() == 5 && !link->txUnackdBytes() &&
      link->receivedPN(13),
    "a later acceptable segment must apply valid header ACK/window and make progress");
  link->teardown();

  auto zero = testLink(&app);
  ZuCHECK(zero->establish(cid), "zero-window segment fixture must establish");
  Zutcp::Frame parsed;
  unsigned used = 0;
  bool immediate = false;
  auto data = dataPkt(0, "12345678", false, parsed, used);
  ZuCHECK(zero->receiveTCPFrame(data, {.seq = 0}, immediate) &&
      zero->rxBytes() == 8 && !zero->rxCreditAvailable(),
    "unread DATA must close the advertised receive window");
  // An empty DATA frame is zero sequence length, independent of its envelope bytes.
  int n = Zutcp::FrameCodec::writeData(frames, sizeof(frames), 8, {}, false);
  ZuCHECK(n > 0 && zero->receiveShort(packet(21, {frames, unsigned(n)}), nullptr,
      {.seq = 8, .flags = 0x10}) && zero->receivedPN(21),
    "zero-length DATA at RCV.NXT must be admissible even with a zero receive window");
  ZuCHECK(zero->windowReply(response, frameBytes) && frameBytes && !zero->ackPending(),
    "accepted empty DATA must commit its ordinary ACK before duplicate-admission checks");
  ZuCHECK(zero->receiveShort(packet(21, {&ping, 1}), nullptr,
      {.seq = 7, .flags = 0x10}) && !zero->ackPending(),
    "duplicate packet number must not bypass sequence admission through duplicate ACK work");
  ZuCHECK(zero->windowReply(response, frameBytes) && !frameBytes &&
      response.ack == 8 && !response.window,
    "zero-window persist response must retain the current edge without ACKing rejected input");
  zero->teardown();
}

void tcpNativeFinish()
{
  ZuTestScopeRT(tcpNativeFinish);
  App app{Zutcp::MinPktPayload, 8};
  app.requireNativeTx = true;
  Zutcp::CxnID cid{"fin--cid"};
  // Exercise both final-ACK orders and reuse of the same native wrapper.
  for (unsigned ackFirst = 0; ackFirst < 2; ++ackFirst) {
    auto link = testLink(&app);
    for (unsigned incarnation = 0; incarnation < 2; ++incarnation) {
      if (incarnation) link->resetRuntimeForTest();
      ZuCHECK(link->establish(cid), "native finish fixture must establish");
      Zutcp::Frame frame;
      unsigned used = 0;
      auto packet = dataPkt(0, "data", true, frame, used);
      ZuCHECK(packet && link->processFrame(frame, packet) == 0 &&
          link->reader().length() == 4 && !link->finSent(),
        "peer EOF must preserve unread DATA and leave local FIN to the application");
      if (ackFirst) link->acceptedFINAckForTest(5);
      link->txCredit(8);
      link->fin();
      Zutcp::SentFrameRef ref;
      ZuCHECK(link->flushRecordedRefs(&ref, 1) == 1 && ref.fin,
        "native finish fixture must transmit its local FIN");
      link->wireTCP({.ack = 1, .window = 8, .flags = 0x10});
      ZuCHECK(link->txCompleted() && link->nativeFinishes == incarnation,
        "local transmit completion must not discard unread receive bytes");
      ZuCHECK(consumeExact_(link->reader(), 4, "data") && link->retireRx(4),
        "native receive reader must drain independently");
      if (!ackFirst) {
        link->acceptedFINAckForTest(4);
        ZuCHECK(link->nativeFinishes == incarnation,
          "ACK preceding the peer FIN must not finish transport retirement");
        link->acceptedFINAckForTest(5);
      }
      ZuCHECK(link->nativeFinishes == incarnation + 1,
        "both FINs and accepted final ACK must finish each transport incarnation");
      link->acceptedFINAckForTest(5);
      ZuCHECK(link->retireRx(0) && link->nativeFinishes == incarnation + 1,
        "repeated ACK and reader notifications must not repeat transport completion");
      link->teardown();
    }
  }

  auto half = testLink(&app);
  ZuCHECK(half->establish(cid), "native half-close fixture must establish");
  Zutcp::Frame frame;
  unsigned used = 0;
  auto packet = dataPkt(0, {}, true, frame, used);
  ZuCHECK(packet && half->processFrame(frame, packet) == 0,
    "native half-close must receive EOF");
  half->acceptedFINAckForTest(1);
  ZuCHECK(!half->finSent() && !half->nativeFinishes,
    "acknowledged peer EOF alone must not initiate local FIN or retire transport");
  half->txCredit(8);
  half->fin();
  Zutcp::SentFrameRef ref;
  ZuCHECK(half->flushRecordedRefs(&ref, 1) == 1 && ref.fin && !half->nativeFinishes,
    "peer completion must wait for local FIN acknowledgment");
  half->wireTCP({.ack = 1, .window = 8, .flags = 0x10});
  ZuCHECK(half->nativeFinishes == 1,
    "local FIN acknowledgment arriving last must complete native transport once");
  half->teardown();
}

void tcpOriginWrap()
{
  ZuTestScopeRT(tcpOriginWrap);
  App app{Zutcp::MinPktPayload, 16};
  auto link = testLink(&app);
  Zutcp::TCPOrigins origins{0xfffffffb, 0xfffffffd, true, true, false};
  Zutcp::CxnID cid{"origincd"};
  ZuCHECK(link->resetOriginsForTest(origins) && link->establish(cid),
    "native wrap fixture must install explicit chosen origins");
  link->txCredit(16);
  auto tx = link->writer_();
  tx << "abcdefgh" << Zi::flush();
  app.requireNativeTx = true;
  link->fin();
  Zutcp::SentFrameRef sent;
  bool emitted = link->flushRecordedRefs(&sent, 1) == 1;
  auto header = link->rangeTCPForTest(sent);
  ZuCHECK(emitted && sent.length == 8 && sent.fin && header.seq == 0xfffffffc &&
      (header.flags & 0x19) == 0x19 && link->txNext() == 9,
    "relative DATA0 must start after SYN and FIN must consume its independent position");
  auto packet = [cid](uint64_t pn, ZuBSpan frames) {
    auto buf = Zutcp::inlineAllocator()->allocBuf();
    int head = Zutcp::Pkt::writeShort(buf->data(), buf->avail(), cid, pn, 2);
    if (head < 0 || frames.length() > buf->avail() - unsigned(head)) return ZmRef<ZiIOBuf>{};
    memcpy(buf->data() + head, frames.data(), frames.length());
    buf->length = unsigned(head) + frames.length();
    return buf;
  };
  uint8_t frames[64];
  int n = Zutcp::FrameCodec::writeData(frames, sizeof(frames), 0, "ABCDEFGH", true);
  ZuCHECK(n > 0 && link->receiveShort(packet(11, {frames, unsigned(n)}), nullptr,
      {.seq = 0xfffffffe, .ack = 5, .window = 16, .flags = 0x19}) &&
      link->reader().length() == 8 && link->rxBytes() == 8 && link->finReceived() &&
      link->rxComplete() && link->txAckd() == 9 && !link->txUnackdBytes(),
    "wire DATA/ACK crossing zero must normalize once and preserve wide byte/FIN accounting");
  auto current = link->localTCP(20, 9, 16);
  auto record = link->closedSnapshotRx({
    ZiSockAddr{ZiIP{"198.18.0.1"}, 1}, ZiSockAddr{ZiIP{"198.18.0.2"}, 2}});
  link->closedSnapshotTx(record);
  ZuCHECK(current.seq == 5 && current.ack == 7 && record.txNext == 5 &&
      record.rxNext == 7 && record.fin,
    "current ACK and minimal history must store wire frontiers including SYN and FIN");
  uint8_t ping = 1;
  ZuCHECK(link->receiveShort(packet(12, {&ping, 1}), nullptr,
      {.seq = 0xfffffffd, .ack = 0x100, .window = 1, .flags = 0x10}) &&
      !link->receivedPN(12) && link->txAckd() == 9 && link->txCreditLimit() == 25,
    "outside-window old wire sequence must not install header ACK/window or packet evidence");
  link->teardown();

  // Peer FIN with a still-open local writer is CLOSE-WAIT, not TIME-WAIT.
  // Keep an original queued byte blocked by zero credit while exercising the
  // actual persist/reset builders; completed graceful close must suppress RST.
  app.requireNativeTx = false;
  auto builders = testLink(&app);
  ZuCHECK(builders->resetOriginsForTest(origins) && builders->establish(cid),
    "packet builders must start with the selected origins");
  builders->txCredit(8);
  auto pending = builders->writer_();
  pending << "abcdefgh" << Zi::flush();
  pending << "i" << Zi::flush();
  app.requireNativeTx = true;
  Zutcp::SentFrameRef first;
  bool firstSent = builders->flushRecordedRefs(&first, 1) == 1 && first.length == 8 && !first.fin;
  bool peerFin = builders->receiveShort(packet(14, {frames, unsigned(n)}), nullptr,
    {.seq = 0xfffffffe, .ack = 0xfffffffc, .window = 0, .flags = 0x19});
  Zutcp::TCPHeader persist, reset;
  ZmRef<ZiIOBuf> resetBuf;
  ZuCHECK(firstSent && peerFin && builders->txRangeCount() == 1 &&
      builders->persistWireForTest(persist) && persist.seq == 3 && persist.ack == 7 &&
      builders->resetWire(reset, resetBuf) && reset.seq == 4 && reset.ack == 7 &&
      reset.flags == 0x14,
    "persist/reset builders must share origins while independent local DATA remains");
  resetBuf = nullptr;
  builders->teardown();

  auto peerReset = testLink(&app);
  ZuCHECK(peerReset->resetOriginsForTest(origins) && peerReset->establish(cid),
    "peer reset fixture must use the same selected origins");
  ZuCHECK(peerReset->receiveShort(packet(13, {&ping, 1}), nullptr,
      {.seq = 0xfffffffe, .ack = 0x1234, .window = 1, .flags = 0x14}) &&
      peerReset->nativeResetAborts == 1 && peerReset->nativeResetPeer &&
      !peerReset->localResetSends && !peerReset->receivedPN(13) && !peerReset->txAckd(),
    "admitted peer reset must use normalized SEQ and ignore its ACK without reflection");
  peerReset->teardown();
}

void tcpNativeHistorySnapshot()
{
  ZuTestScopeRT(tcpNativeHistorySnapshot);
  App app{Zutcp::MinPktPayload, 8};
  app.requireNativeTx = true;
  Zutcp::ClosedTuples history{2};
  for (unsigned v6 = 0; v6 < 2; ++v6) {
    auto link = testLink(&app, true);
    Zutcp::CxnID cid{"old--cid"};
    ZiSockAddr local{ZiIP{v6 ? "2001:db8::1" : "198.18.0.1"}, 443};
    ZiSockAddr remote{ZiIP{v6 ? "2001:db8::2" : "198.18.0.2"}, 12000};
    link->serverPathForTest(local, remote);
    ZuCHECK(link->establish(cid), "native history fixture must establish");
    Zutcp::Frame frame;
    unsigned used = 0;
    auto packet = dataPkt(0, "data", true, frame, used);
    ZuCHECK(packet && link->processFrame(frame, packet) == 0,
      "native history fixture must receive contiguous DATA and FIN");
    auto record = link->closedSnapshotRx(link->closedKeyRx());
    link->closedSnapshotTx(record);
    ZuCHECK(record.key == (Zutcp::ClosedTupleKey{local, remote}) &&
        record.rxNext == 5 && record.fin && record.localCID == cid && record.peerCID == cid,
      "native history must capture actual packet local/remote tuple and peer FIN identity");
    auto remembered = history.remember(record);
    packet = nullptr;
    link->teardown();
    link = nullptr;
    Zutcp::ClosedTuple found;
    ZuCHECK(history.find(record.key, found) && found.id == remembered.id &&
        found.rxNext == 5 && found.localCID == cid,
      "minimal history must remain usable after native owner and packet destruction");
  }
}

void tcpLocalAbort()
{
  ZuTestScopeRT(tcpLocalAbort);
  App app{Zutcp::MinPktPayload, 8};
  app.requireNativeTx = true;
  Zutcp::CxnID cid{"abortcid"};
  auto link = testLink(&app);
  ZuCHECK(link->establish(cid), "fatal native DATA fixture must establish");
  auto packet = [cid](uint64_t pn, ZuBSpan data, bool fin) -> ZmRef<ZiIOBuf> {
    static ZiMultiplex mx;
    auto buf = ZutcpTest::pktRxAllocator(&mx)->allocBuf();
    if (!buf) return {};
    int head = Zutcp::Pkt::writeShort(buf->data(), buf->avail(), cid, pn, 4);
    if (head < 0) return {};
    int n = Zutcp::FrameCodec::writeData(buf->data() + head,
      buf->avail() - unsigned(head), 0, data, fin);
    if (n < 0) return {};
    buf->length = unsigned(head + n);
    return buf;
  };
  ZuCHECK(link->receiveShort(packet(0, {}, true), nullptr,
      {.seq = 0, .window = 8, .flags = 0x11}) && link->rxComplete(),
    "peer FIN must commit the native receive final position");
  // SEQ is in the advertised window, but DATA contradicts the committed FIN.
  ZuCHECK(!link->receiveShort(packet(1, "x", false), nullptr,
      {.seq = 1, .window = 8, .flags = 0x10}) && link->localResetSends == 1 &&
      link->nativeResetAborts == 1 && !link->nativeResetPeer &&
      link->disconnecting_() && !link->timerWork() && link->localResetHeader.flags == 0x14,
    "fatal native DATA must emit meaningful RST and drain without waiting for a policy timer");
  ZuCHECK(link->receiveShort(packet(2, "x", false), nullptr,
      {.seq = 1, .window = 8, .flags = 0x10}) && link->localResetSends == 1 &&
      link->nativeResetAborts == 1,
    "local abort input gate must suppress later native work and repeated aborts");
  link->teardown();

  for (unsigned passive = 0; passive < 2; ++passive) {
    auto graceful = testLink(&app);
    ZuCHECK(graceful->establish(cid), "graceful native cleanup fixture must establish");
    graceful->txCredit(8);
    if (passive) graceful->peerFINForTest();
    graceful->fin();
    Zutcp::SentFrameRef ref;
    ZuCHECK(graceful->flushRecordedRefs(&ref, 1) == 1 && ref.fin,
      "native cleanup fixture must send its FIN before acknowledgment");
    graceful->wireTCP({.ack = 1, .window = 8, .flags = 0x10});
    if (!passive) graceful->peerFINForTest();
    ZuCHECK(graceful->tcpCloseState() == (passive ? Zutcp::TcpCloseState::Closed :
	Zutcp::TcpCloseState::TimeWait),
      "both native close roles must reach their completed FIN state");
    unsigned completed = 0;
    graceful->abortLocalRx_(0, [&graceful, &completed] {
      ++completed;
      graceful->tcpResetAbort_(false);
    });
    ZuCHECK(completed == 1 && !graceful->localResetSends && !graceful->timerWork(),
      "explicit native cleanup after completed FIN must drain without sending RST");
    graceful->teardown();
  }
}

void tcpNativeReset()
{
  ZuTestScopeRT(tcpNativeReset);
  App app{Zutcp::MinPktPayload, 8};
  app.requireNativeTx = true;
  auto link = testLink(&app);
  Zutcp::CxnID cid{"rst--cid"};
  ZuCHECK(link->establish(cid), "native RST fixture must establish");
  link->txCredit(64);
  RawRetxAllocator allocator;
  Zxdp::BufPool pool{1, sizeof(allocator.bytes)};
  ZmRef<Zutcp::TxBacking> source = new Zutcp::TxRanges::Node{};
  source->native = &allocator;
  source->pool = &pool;
  source->data = allocator.bytes;
  source->size = sizeof(allocator.bytes);
  source->payloadOffset = 256;
  source->payloadLength = source->preparedLength = 4;
  source->backing.adopt(&allocator, allocator.bytes, sizeof(allocator.bytes));
  auto retained = Zutcp::SentFrameRef::data(Zutcp::TxRange{source, 256, 4, 0}, false);
  link->recordSentPkt(Zutcp::PktNumSpace::AppData, 0, 4, retained, true);
  ZuCHECK(link->consumeTxCredit(4), "RST fixture must account retained original DATA");
  auto packet = [cid]() -> ZmRef<ZiIOBuf> {
    static ZiMultiplex mx;
    auto buf = ZutcpTest::pktRxAllocator(&mx)->allocBuf();
    if (!buf) return {};
    int n = Zutcp::Pkt::writeShort(buf->data(), buf->avail(), cid, 7, 4);
    if (n < 0) return {};
    // Deliberately invalid diagnostic bytes must not be parsed as envelope frames.
    buf->data()[n] = 0xff;
    buf->length = unsigned(n) + 1;
    return buf;
  };
  auto flushes = link->txFlushQueued;
  ZuCHECK(link->receiveShort(packet(), nullptr,
      {.seq = 8, .ack = 4, .window = 1, .flags = 0x14}) &&
      !link->nativeResetAborts && !link->txAckd() && link->txCreditLimit() == 64 &&
      link->txFlushQueued == flushes && !link->receivedPN(7) && !allocator.returned,
    "out-of-window RST must be silent and cannot apply its ACK/window or retire backing");
  auto sent = link->diag().tx.sentPackets[Zutcp::PktNumSpace::AppData];
  Zutcp::TCPHeader tcp;
  ZmRef<ZiIOBuf> wire;
  ZuCHECK(link->resetWire(tcp, wire) && tcp.flags == 0x14 && tcp.seq == 4 && !tcp.ack &&
      link->diag().tx.sentPackets[Zutcp::PktNumSpace::AppData] == sent &&
      !link->resetWire(tcp, wire),
    "local RST must use one nonreliable control packet without a recovery record");
  ZuCHECK(link->receiveShort(packet(), nullptr,
      {.seq = 0, .ack = 4, .window = 1, .flags = 0x14}) &&
      link->nativeResetAborts == 1 && link->nativeResetPeer &&
      !link->txAckd() && link->txCreditLimit() == 64 && !link->receivedPN(7) &&
      !allocator.returned && *source->backing && link->txFlushQueued == flushes &&
      !link->timerWork(),
    "eligible RST must abort both owners before header ACK/control mutation or reflection");
  ZuCHECK(link->receiveShort(packet(), nullptr, {.seq = 0, .flags = 0x04}) &&
      link->nativeResetAborts == 1,
    "a repeated accepted RST must not repeat owner abort");
  link->teardown();
  wire = nullptr;
  link = nullptr;
  retained = {};
  ZuCHECK(!allocator.returned && *source->backing,
    "independent retained backing must survive connection destruction");
  source = nullptr;
  ZuCHECK(allocator.returned == 1,
    "final independent backing release must return the original allocation exactly once");
}

void tcpSegmentEdges()
{
  ZuTestScopeRT(tcpSegmentEdges);
  App app{Zutcp::MinPktPayload, 8};
  auto link = testLink(&app);
  Zutcp::CxnID cid{"edge-cid"};
  ZuCHECK(link->establish(cid), "segment edge fixture must establish");
  link->txCredit(64);
  RawRetxAllocator allocator;
  Zxdp::BufPool pool{1, sizeof(allocator.bytes)};
  ZmRef<Zutcp::TxBacking> source = new Zutcp::TxRanges::Node{};
  source->native = &allocator;
  source->pool = &pool;
  source->data = allocator.bytes;
  source->size = sizeof(allocator.bytes);
  source->payloadOffset = 256;
  source->payloadLength = source->preparedLength = 4;
  source->backing.adopt(&allocator, allocator.bytes, sizeof(allocator.bytes));
  memcpy(source->data + source->payloadOffset, "sent", 4);
  auto retained = Zutcp::SentFrameRef::data(Zutcp::TxRange{source, 256, 4, 0}, false);
  link->recordSentPkt(Zutcp::PktNumSpace::AppData, 0, 4, retained, true);
  ZuCHECK(link->consumeTxCredit(4), "segment edge fixture must account retained DATA");
  app.requireNativeTx = true;
  auto packet = [cid](uint64_t pn, ZuBSpan data, bool fin = false) -> ZmRef<ZiIOBuf> {
    static ZiMultiplex mx;
    auto buf = ZutcpTest::pktRxAllocator(&mx)->allocBuf();
    if (!buf) return {};
    int head = Zutcp::Pkt::writeShort(buf->data(), buf->avail(), cid, pn, 2);
    if (head < 0) return {};
    int n = Zutcp::FrameCodec::writeData(buf->data() + head,
      buf->avail() - unsigned(head), 777, data, fin);
    if (n < 0) return {};
    buf->length = unsigned(head + n);
    return buf;
  };
  auto prefix = packet(30, "XXabcd");
  auto prefixData = prefix ? prefix->data() + prefix->length - 6 : nullptr;
  ZuCHECK(prefix && link->receiveShort(prefix, nullptr,
      {.seq = 0xfffffffe, .window = 64, .flags = 0x10}) && link->receivedPN(30) &&
      link->rxBytes() == 4 && link->rxAdmitted() == 4 &&
      link->reader().span().data() == prefixData + 2 && !allocator.returned &&
      *source->backing && source->data == allocator.bytes,
    "short ingress crossing sequence wrap must retain only the original novel suffix");
  auto old = packet(31, "XX");
  ZuCHECK(old && link->receiveShort(old, nullptr,
      {.seq = 0xfffffffa, .ack = 4, .window = 1, .flags = 0x10}) &&
      !link->receivedPN(31) && !link->txAckd() && link->txCreditLimit() == 64 &&
      link->txUnackdBytes() == 4 && !allocator.returned && *source->backing &&
      source->data == allocator.bytes && !memcmp(source->data + 256, "sent", 4),
    "a wholly old DATA segment must not retire or rewrite original retained Tx backing");
  auto suffix = packet(32, "ghijkl", true);
  auto suffixData = suffix ? suffix->data() + suffix->length - 6 : nullptr;
  ZuCHECK(suffix && link->receiveShort(suffix, nullptr,
      {.seq = 6, .window = 64, .flags = 0x11}) && link->receivedPN(32) &&
      link->rxPending() == 1 && link->rxAdmitted() == 6 && !link->finReceived(),
    "partially overlapping short ingress must clip right-edge DATA and its out-of-window FIN");
  auto gap = packet(33, "ef");
  ZuCHECK(gap && link->receiveShort(gap, nullptr,
      {.seq = 4, .window = 64, .flags = 0x10}) && link->receivedPN(33) &&
      link->rxBytes() == 8 && !link->rxPending() && !link->finReceived() &&
      consumeExact_(link->reader(), 4, "abcd") && consumeExact_(link->reader(), 2, "ef") &&
      link->reader().span().data() == suffixData &&
      consumeExact_(link->reader(), 2, "gh") && !link->rxCreditAvailable(),
    "gap filling must deliver exact clipped bytes at their original packet addresses");
  auto blocked = packet(34, "q");
  auto fin = packet(35, {}, true);
  ZuCHECK(blocked && fin && link->receiveShort(blocked, nullptr,
      {.seq = 8, .ack = 4, .window = 1, .flags = 0x10}) &&
      link->receiveShort(fin, nullptr, {.seq = 8, .ack = 4, .window = 1, .flags = 0x11}) &&
      !link->receivedPN(34) && !link->receivedPN(35) && !link->finReceived() &&
      link->rxAdmitted() == 8 && !link->rxPending() && !link->txAckd() &&
      link->txCreditLimit() == 64 && !allocator.returned && *source->backing &&
      source->data == allocator.bytes && !memcmp(source->data + 256, "sent", 4),
    "zero-window DATA and FIN must be refused before header ACK or backing-retirement effects");
  auto ack = packet(36, {});
  ZuCHECK(ack && link->receiveShort(ack, nullptr,
      {.seq = 8, .ack = 4, .window = 1, .flags = 0x10}) && link->receivedPN(36) &&
      link->txAckd() == 4 && !link->txUnackdBytes() && allocator.valid &&
      allocator.returned == 1 && !*source->backing && !source->data,
    "an admissible empty segment must release the same original backing exactly once");
  link->teardown();
}

void tcpWindowState()
{
  ZuTestScopeRT(tcpWindowState);
  App app;
  app.testNow = Zm::now() + ZuTime{1000};
  auto link = testLink(&app);
  ZuCHECK(link->establish({}), "TCP window fixture must establish");
  link->txCredit(64);
  auto ref = link->prepareDataRef(0, 0, 32);
  link->acceptTxData(ref);
  ZuCHECK(link->consumeTxCredit(32), "TCP window fixture must consume sent bytes");
  app.requireNativeTx = true;
  ZuCHECK(!link->queueMaxData(8192) && !link->queuedControlFrames(),
    "native control output must not queue legacy absolute-credit records");
  link->wireTCP({.seq = 10, .ack = 16, .window = 0, .flags = 0x10});
  ZuCHECK(link->txAckd() == 16 && link->txCreditLimit() == 16 &&
      !link->txCreditAvailable() && link->txUnackdBytes() == 16,
    "header ACK must retire a partial prefix while a shrunken window preserves sent accounting");
  auto cwnd = link->diag().tx.congestionWindow;
  app.testNow += ZuTime{1};
  link->deliverPersist();
  app.testNow += ZuTime{10};
  link->deliverRTO();
  ZuCHECK(link->probes == 1 && link->probeValid && !link->retransmits &&
      link->diag().tx.congestionWindow == cwnd,
    "a responsive zero window with outstanding DATA must use persist without RTO loss response");
  link->packetAck(32, 0);
  ZuCHECK(link->txAckd() == 16 && link->txUnackdBytes() == 16,
    "native packet ACK content cannot override TCP byte acknowledgment");
  link->wireTCP({.seq = 9, .ack = 32, .window = 64, .flags = 0x10});
  ZuCHECK(link->txAckd() == 32 && link->txCreditLimit() == 16 && !link->txCreditAvailable(),
    "an older segment may acknowledge bytes but cannot replace a newer advertised window");
  link->wireTCP({.seq = 10, .ack = 32, .window = 8, .flags = 0x10});
  ZuCHECK(link->txCreditLimit() == 40 && link->txCreditAvailable() == 8,
    "new ACK/window evidence must reopen the original sent-byte position without resetting it");
  link->wireTCP({.seq = 10, .ack = 16, .window = 64, .flags = 0x10});
  link->wireTCP({.seq = 11, .ack = 33, .window = 64, .flags = 0x10});
  ZuCHECK(link->txAckd() == 32 && link->txCreditAvailable() == 8,
    "old or future ACKs must not move the window beyond accepted sequence space");
  auto local = link->localTCP(2, 12, 20);
  auto stale = link->localTCP(1, 0, 65535);
  ZuCHECK(local.ack == 12 && local.window == 8 && stale.ack == 12 && stale.window == 8,
    "older Rx snapshots cannot overwrite current TCP acknowledgment/window output");
  link->teardown();
  auto wrap = testLink(&app);
  ZuCHECK(wrap->establish({}), "TCP window wrap fixture must establish");
  wrap->wireTCP({.seq = 0xfffffffc, .window = 8, .flags = 0x10});
  wrap->wireTCP({.seq = 2, .window = 16, .flags = 0x10});
  wrap->wireTCP({.seq = 0xfffffffe, .window = 64, .flags = 0x10});
  ZuCHECK(wrap->txCreditAvailable() == 16 &&
      Zutcp::tcpPosition(3, (uint64_t(1)<<32) - 2) == (uint64_t(1)<<32) + 3 &&
      Zutcp::tcpPosition(0xfffffffe, (uint64_t(1)<<32) + 2) == (uint64_t(1)<<32) - 2 &&
      ZuNull(Zutcp::tcpPosition(0xffffffff, 0)),
    "modulo window ordering must lift at the wide frontier and reject before-origin data");
  wrap->teardown();
}

struct WindowPacketAllocator : Zi::IOBufAllocator {
  alignas(ZiIOBuf_Align) uint8_t bytes[2048];
  mutable unsigned returned = 0;
  mutable bool valid = true;

  explicit WindowPacketAllocator(ZiMultiplex *mx) :
    IOBufAllocator{mx, Zi::IOBufType::TxTCP, sizeof(bytes), 256} {
    memset(bytes, 0xa5, sizeof(bytes));
  }
  uint8_t *alloc(unsigned) const override { return nullptr; }
  void free(uint8_t *p, unsigned n, bool registered) const override {
    valid &= p == bytes && n == sizeof(bytes) && registered;
    ++returned;
  }
  ZmRef<ZiIOBuf> allocBuf(void *, uint32_t, uint32_t) override { return nullptr; }
  ZmRef<ZiIOBuf> packet(unsigned length) {
    ZmRef<ZiIOBuf> buf = new Zi::IOBufStub<ZiIOBuf>{
      bytes, unsigned(sizeof(bytes)), this, nullptr, length, false, true};
    buf->skip = 256;
    return buf;
  }
};

void positiveWindowPacket(bool single)
{
  ZuTestScopeRT(positiveWindowPacket);
  App app;
  auto link = testLink(&app);
  WindowPacketAllocator first{app.mx()}, second{app.mx()};
  Zxdp::BufPool firstPool{1, sizeof(first.bytes)}, secondPool{1, sizeof(second.bytes)};
  ZuCHECK(link->establish({}), "positive-window fixture must establish");
  link->txCredit(2000);
  auto packet = first.packet(1000);
  auto origin = packet->data();
  uint8_t expected[1000];
  for (unsigned i = 0; i < sizeof(expected); ++i) expected[i] = uint8_t(i * 13 + 7);
  memcpy(packet->data(), expected, sizeof(expected));
  ZuCHECK(link->enqueue(ZuMv(packet)) && link->enqueue(second.packet(17)),
    "two original registered packets must enter the existing producer queue");
  Zutcp::TxRange queued;
  bool nativePools = link->txRange(0, queued) && queued.storage->native == &first;
  if (nativePools) queued.storage->pool = &firstPool;
  nativePools &= link->txRange(1, queued) && queued.storage->native == &second;
  if (nativePools) queued.storage->pool = &secondPool;
  queued = {};
  ZuCHECK(nativePools, "registered recovery must retain its native pool identity");
  app.requireNativeTx = true;
  link->wireTCP({.window = 0, .flags = 0x10});
  Zutcp::SentFrameRef original, next;
  auto flush = [&link, single](Zutcp::SentFrameRef &ref) {
    return single ? link->flushCongestedData(&ref) : bool(link->flushRecordedRefs(&ref, 1));
  };
  (void)flush(original);
  ZuCHECK(link->txRangeCount() == 2 && link->txBufferedBytes() == 1017 &&
      !link->txNext() && !first.returned && !second.returned,
    "zero credit must preserve both original packets before packetization");
  link->wireTCP({.seq = 1, .window = 400, .flags = 0x10});
  link->fin();
  ZuCHECK(flush(original) && original.length == 1000 && !original.fin &&
      original.range.storage && original.range.storage->native == &first &&
      original.range.storage->data + original.range.offset == origin &&
      original.range.storage->payloadLength == 1000 &&
      link->txNext() == 1000 && link->txCreditLimit() == 400 &&
      !link->txCreditAvailable() && link->txRangeCount() == 1 &&
      link->txBufferedBytes() == 17 && !link->finReady() && !first.returned,
    "positive 400-byte credit must send the original 1000 bytes and charge every byte");
  link->wireTCP({.seq = 2, .window = 400, .flags = 0x10});
  (void)flush(next);
  link->wireTCP({.seq = 3, .ack = 400, .window = 0, .flags = 0x10});
  (void)flush(next);
  link->wireTCP({.seq = 4, .ack = 400, .window = 400, .flags = 0x10});
  (void)flush(next);
  link->reclaimData(400);
  Zutcp::PktNumSpace::T level;
  Zutcp::SentFrameRef covering;
  bool retained = link->nextRetransmitRef(level, covering);
  ZuCHECK(retained && covering.length == 1000 && covering.offset == 0 &&
      covering.range.storage == original.range.storage &&
      covering.range.storage->data + covering.range.offset == origin &&
      !memcmp(origin, expected, sizeof(expected)) && !first.returned,
    "a partial ACK must retain the full immutable original for covering-packet recovery");
  ZuCHECK(link->txNext() == 1000 && link->txRangeCount() == 1 &&
      link->txBufferedBytes() == 17 && !link->txCreditAvailable() && !second.returned,
    "stale positive and smaller absolute edges must never authorize another new packet");
  link->wireTCP({.seq = 5, .ack = 1000, .window = 1, .flags = 0x10});
  ZuCHECK(flush(next) && next.length == 17 && !next.fin && next.offset == 1000 &&
      next.range.storage && next.range.storage->native == &second &&
      link->txNext() == 1017 && !link->txCreditAvailable() && !link->finReady(),
    "a newly positive edge must admit exactly the next original packet without its FIN");
  link->wireTCP({.seq = 6, .ack = 1017, .window = 0, .flags = 0x10});
  ZuCHECK(!link->finReady() && !link->finDequeued(),
    "ACKing overshot DATA at zero window must not admit FIN's sequence position");
  link->wireTCP({.seq = 7, .ack = 1017, .window = 1, .flags = 0x10});
  Zutcp::SentFrameRef fin;
  ZuCHECK(flush(fin) && !fin.length && fin.offset == 1017 && fin.fin &&
      link->txNext() == 1018 && link->finDequeued(),
    "FIN must wait for its own reopened byte after all original DATA");
  link->wireTCP({.seq = 8, .ack = 1018, .window = 0, .flags = 0x10});
  link->teardown();
  original = next = fin = covering = {};
  ZuCHECK(first.valid && second.valid && first.returned == 1 && second.returned == 1,
    "full ACK and owner drain must return each original registered allocation once");
}

void positiveWindowReceive()
{
  ZuTestScopeRT(positiveWindowReceive);
  App app{Zutcp::MinPktPayload, 400};
  app.requireNativeTx = true;
  auto link = testLink(&app);
  uint8_t expected[1000];
  for (unsigned i = 0; i < sizeof(expected); ++i) expected[i] = uint8_t(i * 13 + 7);
  Zutcp::Frame frame;
  unsigned used = 0;
  bool immediate = false;
  auto packet = dataPkt(0, span_(expected, sizeof(expected)), false, frame, used);
  auto origin = frame.payload.data();
  ZuCHECK(packet && link->receiveTCPFrame(packet, {.seq = 0}, immediate) &&
      link->rxBytes() == 400 && link->rxDataCreditUsed() == 400 &&
      !link->rxCreditAvailable() && !link->finReceived(),
    "strict receive credit must clip an overshooting original packet to 400 novel bytes");
  ZuCHECK(link->receiveTCPFrame(packet, {.seq = 0}, immediate) && immediate &&
      link->rxBytes() == 400 && !link->rxCreditAvailable(),
    "a duplicate original at zero window must not admit its outside tail");
  ZuCHECK(consumeExact_(link->reader(), 400, span_(expected, 400)) && link->retireRx(400),
    "the first bounded slice must deliver exact bytes before reopening");
  ZuCHECK(link->receiveTCPFrame(packet, {.seq = 0}, immediate) &&
      link->rxBytes() == 800 && link->rxDataCreditUsed() == 800 &&
      !link->rxCreditAvailable() &&
      consumeExact_(link->reader(), 400, span_(expected + 400, 400)) && link->retireRx(400),
    "the same covering original must supply only the next 400 bytes after reopening");
  ZuCHECK(link->receiveTCPFrame(packet, {.seq = 0}, immediate) &&
      link->rxBytes() == 1000 && link->rxDataCreditUsed() == 1000 &&
      consumeExact_(link->reader(), 200, span_(expected + 800, 200)) &&
      link->retireRx(200) && frame.payload.data() == origin &&
      !memcmp(origin, expected, sizeof(expected)),
    "replayed original backing must deliver the exact final tail without duplicates or mutation");
  link->teardown();
}

void tcpFinWindow()
{
  ZuTestScopeRT(tcpFinWindow);
  App app;
  app.testNow = Zm::now() + ZuTime{1000};
  auto link = testLink(&app);
  ZuCHECK(link->establish({}), "FIN window fixture must establish");
  link->txCredit(8);
  {
    auto tx = link->writer_();
    tx << "12345678" << Zi::flush();
  }
  // Plaintext fixture storage exercises the native window policy below.
  app.requireNativeTx = true;
  link->wireTCP({.window = 8, .flags = 0x10});
  link->fin();
  Zutcp::SentFrameRef refs[2];
  unsigned n = link->flushRecordedRefs(refs, 2);
  ZuCHECK(n == 1 && refs[0].length == 8 && !refs[0].fin &&
      link->txNext() == 8 && !link->finDequeued() && !link->finReady(),
    "DATA filling the right edge must leave FIN queued without segmenting its backing");
  link->wireTCP({.ack = 8, .window = 0, .flags = 0x10});
  ZuCHECK(link->txAckd() == 8 && !link->txUnackdBytes() &&
      !link->txRangeCount() && !link->finReady() && !link->flushRecordedRefs(refs, 2),
    "a zero-window ACK of all DATA must not admit FIN's independent sequence position");
  unsigned probes = link->probes;
  app.testNow += ZuTime{1};
  link->deliverPersist();
  ZuCHECK(link->probes == probes + 1 && link->probeValid &&
      !link->finDequeued() && link->txNext() == 8,
    "a queued FIN alone must keep zero-window persist active without sending FIN");
  link->wireTCP({.seq = 1, .ack = 8, .window = 1, .flags = 0x10});
  n = link->flushRecordedRefs(refs, 2);
  ZuCHECK(n == 1 && !refs[0].length && refs[0].offset == 8 && refs[0].fin &&
      link->finDequeued() && link->txNext() == 9 && !link->flushRecordedRefs(refs, 2),
    "one reopened byte must admit exactly one FIN after the original DATA");
  link->wireTCP({.seq = 1, .ack = 9, .window = 0, .flags = 0x10});
  probes = link->probes;
  app.testNow += ZuTime{60};
  link->deliverPersist();
  ZuCHECK(link->txAckd() == 9 && link->txCompleted() && link->probes == probes,
    "FIN acknowledgment must cancel persist without consuming application flow credit");
  link->teardown();

  auto empty = testLink(&app);
  ZuCHECK(empty->establish({}), "empty FIN fixture must establish");
  empty->txCredit(0);
  empty->wireTCP({.window = 0, .flags = 0x10});
  empty->fin();
  ZuCHECK(!empty->finReady() && !empty->flushRecordedRefs(refs, 2),
    "an empty close must respect an already closed receive window");
  app.testNow += ZuTime{1};
  empty->deliverPersist();
  ZuCHECK(empty->probes == 1 && empty->probeValid,
    "FIN initiation after a zero-window advertisement must arm persist");
  empty->wireTCP({.seq = 1, .window = 1, .flags = 0x10});
  n = empty->flushRecordedRefs(refs, 2);
  ZuCHECK(n == 1 && !refs[0].offset && !refs[0].length && refs[0].fin &&
      empty->txNext() == 1,
    "reopening an empty close must send FIN at its own sequence position");
  empty->teardown();

  auto combined = testLink(&app);
  ZuCHECK(combined->establish({}), "combined DATA/FIN fixture must establish");
  combined->txCredit(9);
  app.requireNativeTx = false;
  {
    auto tx = combined->writer_();
    tx << "12345678" << Zi::flush();
  }
  app.requireNativeTx = true;
  combined->wireTCP({.window = 9, .flags = 0x10});
  combined->fin();
  n = combined->flushRecordedRefs(refs, 2);
  ZuCHECK(n == 1 && refs[0].length == 8 && refs[0].fin &&
      combined->finDequeued() && combined->txNext() == 9,
    "DATA and FIN must share the original packet when both fit the peer window");
  combined->teardown();
}

void windowPersist()
{
  ZuTestScopeRT(windowPersist);
  App app;
  app.testNow = Zm::now() + ZuTime{1000};
  ZuTime start = app.testNow;
  auto link = testLink(&app);
  ZuCHECK(link->establish({}), "persist fixture must establish");
  link->txCredit(0);
  auto buf = Zutcp::inlineAllocator()->allocBuf(nullptr, 4);
  memcpy(buf->data(), "more", 4);
  ZuCHECK(link->enqueue_(buf) && link->txRangeCount() && !link->queuedControlFrames(),
    "a zero-window send must retain DATA without reliable blocked-control history");
  uint64_t cwnd = link->diag().tx.congestionWindow;
  app.testNow = start + Zutcp::timeUS(999999);
  link->deliverPersist();
  ZuCHECK(!link->probes, "persist must honor its one-second initial floor");
  for (unsigned due : {1u, 3u, 7u, 15u, 31u, 63u, 123u, 183u}) {
    app.testNow = start + ZuTime{due} - Zutcp::timeUS(1);
    unsigned before = link->probes;
    link->scheduleDataForTest();
    link->deliverPersist();
    ZuCHECK(link->probes == before, "repeated writable work must not reset or advance persist");
    app.testNow += Zutcp::timeUS(1);
    link->probeAllowed = due != 15;
    link->deliverPersist();
    link->probeAllowed = true;
    ZuCHECK(link->probes == before + 1 && link->probeValid,
      "persist must send the current edge at 1/2/4/8/16/32/60-second intervals");
    link->ackOnly(before);
    auto diag = link->diag();
    ZuCHECK(diag.tx.congestionWindow == cwnd && !diag.tx.bytesInFlight &&
	!link->ptos && !link->retransmits && !link->queuedControlFrames(),
      "probe/response evidence must not create loss flight, reduce cwnd or run DATA recovery");
  }
  unsigned probes = link->probes;
  link->grantDataCredit(4);
  Zutcp::SentFrameRef refs[2];
  ZuCHECK(link->dataReadyForTest() && link->flushSentRefs(refs, 2) == 1 &&
      refs[0].kind == Zutcp::SentFrameKind::Data && refs[0].length == 4,
    "usable credit must make pending DATA eligible immediately without waiting for persist");
  app.testNow += ZuTime{120};
  link->deliverPersist();
  ZuCHECK(link->probes == probes, "window reopening must cancel a queued persist callback");
  link->txCredit(4);
  link->consumeTxCredit(4);
  auto next = Zutcp::inlineAllocator()->allocBuf(nullptr, 1);
  ZuCHECK(link->enqueue_(next), "second persist episode must retain its source");
  link->reset(7);
  app.testNow += ZuTime{120};
  link->deliverPersist();
  ZuCHECK(link->probes == probes, "reset must cancel pending persist work");
  link->teardown();
  App slow{Zutcp::MinPktPayload, 0, ZuTime{3}};
  slow.testNow = Zm::now() + ZuTime{1000};
  start = slow.testNow;
  auto other = testLink(&slow);
  ZuCHECK(other->establish({}), "configured-RTO persist fixture must establish");
  other->txCredit(0);
  ZuCHECK(other->enqueue_(Zutcp::inlineAllocator()->allocBuf(nullptr, 1)),
    "configured-RTO persist fixture must retain DATA");
  slow.testNow = start + ZuTime{1};
  other->deliverPersist();
  ZuCHECK(!other->probes, "persist initial interval must respect a larger current RTO");
  slow.testNow = start + ZuTime{3};
  other->deliverPersist();
  ZuCHECK(other->probes == 1 && other->probeValid,
    "a three-second initial RTO must select a three-second first persist interval");
  other->teardown();
  auto partial = testLink(&app);
  ZuCHECK(partial->establish({}), "single-packet persist fixture must establish");
  partial->txCredit(2);
  ZuCHECK(partial->enqueue_(Zutcp::inlineAllocator()->allocBuf(nullptr, 4)) &&
      partial->flushCongestedData() && partial->txBufferedBytes() == 2 &&
      !partial->txCreditAvailable(),
    "the single-packet API must exhaust credit while preserving its unsent suffix");
  partial->ackBytes(2, 0);
  app.testNow += ZuTime{1};
  partial->deliverPersist();
  ZuCHECK(partial->probes == 1 && partial->probeValid,
    "single-packet credit exhaustion must arm persist without another flush or DATA RTO");
  partial->teardown();
}

void windowUpdateGeneration()
{
  ZuTestScopeRT(windowUpdateGeneration);
  App app;
  auto link = testLink(&app);
  app.deferredTx = true;
  bool accepted = false;
  app.onTx([link, &accepted] {
    Zutcp::Frame probe;
    probe.type = Zutcp::FrameType::DataBlocked;
    probe.value = link->rxCreditLimit();
    accepted = link->receiveDataBlocked(probe);
    link->resetRuntimeForTest(); // its earlier queued advertisement is now stale
  });
  app.drainTx();
  ZuCHECK(accepted && !link->queuedControlFrames(),
    "a queued window response from an earlier generation must not update the reset connection");
  app.onTx([link, &accepted] {
    Zutcp::Frame probe;
    probe.type = Zutcp::FrameType::DataBlocked;
    probe.value = link->rxCreditLimit();
    accepted = link->receiveDataBlocked(probe);
  });
  app.drainTx();
  ZuCHECK(accepted && link->queuedControlFrames() == 1,
    "the new generation must still publish its current receive edge");
  link->teardown();
}

void windowProbeReply()
{
  ZuTestScope(windowProbeReply);
  App app{Zutcp::MinPktPayload, 8};
  auto receiver = testLink(&app);
  auto sender = testLink(&app);
  sender->txCredit(8);
  ZuCHECK(sender->consumeTxCredit(8) && !sender->txCreditAvailable(),
    "sender must exhaust the initial byte window");
  Zutcp::Frame frame;
  unsigned used = 0;
  auto packet = dataPkt(0, "abcdefgh", false, frame, used);
  ZuCHECK(packet && !receiver->receiveFrame(frame, packet) &&
      !receiver->rxCreditAvailable() && !receiver->rxRetired(),
    "unconsumed input must exhaust receive credit without returning bytes");
  frame = {};
  frame.type = Zutcp::FrameType::DataBlocked;
  frame.value = 8;
  Zutcp::SentFrameRef refs[2];
  ZuCHECK(receiver->receiveDataBlocked(frame) &&
      receiver->flushSentRefs(refs, 2) == 1 &&
      refs[0].controlType == Zutcp::FrameType::MaxData && refs[0].value == 8 &&
      !receiver->rxRetired() && !receiver->rxCreditAvailable(),
    "a closed-window probe must re-advertise the existing edge without granting bytes");
  receiver->reader().advance(8);
  ZuCHECK(receiver->retireRx(8) && receiver->rxCreditLimit() == 16 &&
      receiver->flushSentRefs(refs, 2) == 1 && refs[0].value == 16,
    "application consumption must publish the opening update");
  // Drop that update. A later probe carries the sender's old exhausted edge.
  ZuCHECK(!sender->txCreditAvailable() && receiver->receiveDataBlocked(frame) &&
      receiver->flushSentRefs(refs, 2) == 1 && refs[0].value == 16 &&
      receiver->rxRetired() == 8,
    "a probe after a lost opening update must recover the current edge");
  sender->grantDataCredit(refs[0].value);
  ZuCHECK(sender->txCreditAvailable() == 8 &&
      receiver->receiveDataBlocked(frame) && receiver->receiveDataBlocked(frame) &&
      receiver->queuedControlFrames() == 1 &&
      receiver->flushSentRefs(refs, 2) == 1 && refs[0].value == 16 &&
      receiver->rxRetired() == 8,
    "repeated probes must coalesce the same response and never retire credit twice");
  frame.value = 17;
  ZuCHECK(!receiver->receiveDataBlocked(frame) && !receiver->queuedControlFrames(),
    "an invalid probe must not queue a window response");
  receiver->teardown();
  sender->teardown();
}

void testMaxBlockedValidate()
{
  ZuTestScope(testMaxBlockedValidate);
  App app;
  auto cxn = testLink(&app);
  uint8_t out[32];
  Zutcp::Frame frame;
  int n = Zutcp::FrameCodec::writeDataBlocked(out, sizeof(out), cxn->rxCreditLimit());
  ZuCHECK(parseFrame(out, n, frame) && cxn->receiveDataBlocked(frame),
    "blocking advice at the connection window must be accepted");
  n = Zutcp::FrameCodec::writeDataBlocked(out, sizeof(out), cxn->rxCreditLimit() + 1);
  ZuCHECK(parseFrame(out, n, frame) && !cxn->receiveDataBlocked(frame),
    "blocking advice beyond the connection window must be rejected");
}

void frameRoleAndSpaceLegality()
{
  ZuTestScope(frameRoleAndSpaceLegality);
  App app;
  auto link = testLink(&app);
  Zutcp::Frame frame;
  frame.type = Zutcp::FrameType::Ping;
  ZuCHECK(link->frameLegal(Zutcp::PktNumSpace::Initial, frame) &&
      link->frameLegal(Zutcp::PktNumSpace::AppData, frame),
    "PING must be legal in setup and established packets");
  frame.type = Zutcp::FrameType::Data;
  ZuCHECK(!link->frameLegal(Zutcp::PktNumSpace::Initial, frame) &&
      link->frameLegal(Zutcp::PktNumSpace::AppData, frame),
    "connection data must require the established packet space");
  frame.type = Zutcp::FrameType::Ack;
  ZuCHECK(link->frameLegal(Zutcp::PktNumSpace::Initial, frame) &&
      !link->frameLegal(Zutcp::PktNumSpace::AppData, frame),
    "a bootstrap packet ACK cannot acknowledge established DATA");
  frame.byteNext = 0;
  ZuCHECK(!link->frameLegal(Zutcp::PktNumSpace::Initial, frame) &&
      link->frameLegal(Zutcp::PktNumSpace::AppData, frame),
    "established ACKs carry an explicit cumulative byte frontier");
  for (uint8_t removed : {0x18, 0x19, 0x1a, 0x1b}) {
    unsigned used = 0;
    ZuCHECK(!Zutcp::FrameCodec::parse({&removed, 1}, frame, used) &&
        used == 1 && frame.type == Zutcp::FrameType::Unknown &&
        !link->frameLegal(Zutcp::PktNumSpace::Initial, frame) &&
        !link->frameLegal(Zutcp::PktNumSpace::AppData, frame),
      "removed CID/path wire codes must fail admission in both spaces");
  }
  frame.type = Zutcp::FrameType::Unknown;
  ZuCHECK(!link->frameLegal(Zutcp::PktNumSpace::Initial, frame) &&
      !link->frameLegal(Zutcp::PktNumSpace::AppData, frame),
    "removed and unknown frames must be rejected before admission");
  link->teardown();
  }

#ifdef Zutcp_DEBUG

void testRxQLog()
{
  ZuTestScope(testRxQLog);

  Zi::Path path = testPath("ZutcpDataRuntimeQLog.sqlog");
  ZiFile::remove(path);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  Zutcp::CxnID cid{"qlogcid1"};
  ZuCHECK(link->establish(cid),
    "test plaintext setup failed");

  bool disabledSeen = true;
  ZuCHECK(link->receiveShort(shortPing(cid, 1), &disabledSeen),
    "disabled-qlog receive packet was rejected");
  ZuCHECK(!disabledSeen, "disabled qlog constructed receive accumulator");

	ZutcpLogParams params;
	params.enabled(true).path(path).ringSize(1<<15);
	ZuCHECK(ZutcpLogger::init(
	    app.qlogTrace(), params, Zutcp::Vantage::Unknown),
	  "runtime qlog init failed");
	ZutcpLogger::start();
  ZuCHECK(ZutcpLogger::enabled(), "runtime qlog did not enable");

  bool enabledSeen = false;
  ZuCHECK(link->receiveShort(shortPing(cid, 2), &enabledSeen),
    "enabled-qlog receive packet was rejected");
  ZuCHECK(enabledSeen, "enabled qlog did not construct receive accumulator");

  ZmRef<ZiIOBuf> bad = shortPing(cid, 3);
  ZuCHECK(bad && bad->length, "runtime qlog corrupt packet setup failed");
  bad->data_()[bad->length - 1] = 0x06;
  ZuCHECK(!link->receiveShort(ZuMv(bad)),
    "runtime qlog corrupt packet was accepted");

	closeQLog(app.qlogTrace());
	ZutcpLogger::stop();
	ZutcpLogDiag diag = ZutcpLogger::diag();
	ZutcpLogger::final(app.qlogTrace());
  link->teardown();

  ZuCHECK(diag.recordsEnqueued >= 2, "runtime qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 3, "runtime qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "runtime qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "runtime qlog output was not written");
  ZuCHECK(parseJSONSeq(data) >= 2, "runtime qlog JSON-SEQ parse failed");
  ZuCHECK(data.find<"quic:packet_received">() >= 0,
    "runtime packet_received qlog missing");
  ZuCHECK(data.find<"\"packet_number\":2">() >= 0,
    "runtime packet number qlog missing");
  ZuCHECK(data.find<"\"frame_type\":\"ping\"">() >= 0,
    "runtime PING frame qlog missing");
  ZuCHECK(data.find<"\"frame_count\":">() >= 0,
    "runtime frame count qlog missing");
  ZuCHECK(data.find<"\"frames_truncated\":">() >= 0,
    "runtime frame truncation qlog missing");
  ZuCHECK(data.find<"quic:packet_dropped">() >= 0,
    "runtime packet_dropped qlog missing");

  ZiFile::remove(path);
}

void testFlowControlQLog()
{
  ZuTestScope(testFlowControlQLog);
  Zi::Path path = testPath("ZutcpDataFlowTest.sqlog");
  ZiFile::remove(path);
  ZutcpLogParams params;
  params.enabled(true).path(path).ringSize(1<<15);
  App app;
  ZuCHECK(ZutcpLogger::init(app.qlogTrace(), params, Zutcp::Vantage::Unknown),
    "flow-control qlog init failed");
  ZutcpLogger::start();
  auto cxn = testLink(&app);
  ZuCHECK(cxn->establish({}), "flow-control fixture must establish");
  for (unsigned turn = 0; turn < 2; ++turn) {
    if (turn) cxn->consumeTxCredit(cxn->txCreditAvailable());
    { auto writer = cxn->writer_(); writer << "blocked" << Zi::flush(); }
    ZuCHECK(!cxn->flushCongestedData() && cxn->txRangeCount() &&
      !cxn->queuedControlFrames(), "connection credit must block DATA without reliable advice");
    cxn->grantDataCredit((turn + 1) << 5);
    ZuCHECK(!cxn->queuedControlFrames() && cxn->flushCongestedData(),
      "new connection credit must clear advice and resume output");
  }
  closeQLog(app.qlogTrace());
  ZutcpLogger::stop();
  auto diag = ZutcpLogger::diag();
  ZutcpLogger::final(app.qlogTrace());
  cxn->teardown();
  auto data = readFile_(path);
  ZuCHECK(diag.recordsEnqueued >= 4 && diag.recordsWritten >= 5 &&
    !diag.writerFailures && parseJSONSeq(data) >= 5,
    "flow-control transitions did not drain as valid JSON-SEQ");
  ZuCHECK(data.find<"connection_data_blocked_updated">() >= 0 &&
    data.find<"connection_flow_control">() >= 0,
    "connection flow-control event/reason missing");
  ZuCHECK(data.find<"stream_data_blocked_updated">() < 0 &&
    data.find<"stream_flow_control">() < 0 && data.find<"stream_id">() < 0,
    "removed per-stream flow events or identity were emitted");
}

void testPTOQLog()
{
  ZuTestScope(testPTOQLog);

  Zi::Path path = testPath("ZutcpDataPTOQLog.sqlog");
  ZiFile::remove(path);

	ZutcpLogParams params;
	params.enabled(true).path(path).ringSize(1<<15);

	App app;
	ZuCHECK(ZutcpLogger::init(
	    app.qlogTrace(), params, Zutcp::Vantage::Unknown),
	  "PTO qlog init failed");
	ZutcpLogger::start();
	ZmRef<TestLink> link = testLink(&app);
  ZuCHECK(link->establish({}), "plaintext connection setup failed");
  link->recordSentPkt(
    Zutcp::PktNumSpace::AppData, 7, Zutcp::MinPktPayload,
    Zutcp::SentFrameRef::control(), true);
  link->sendAckEliciting(10);
  link->ackOnly(10);

  link->recordSentPkt(
    Zutcp::PktNumSpace::AppData, 20, Zutcp::MinPktPayload,
    Zutcp::SentFrameRef::control(), true);
  Zutcp::PktNumSpace::T level = Zutcp::PktNumSpace::Initial;
  unsigned probes = 0;
  ZuCHECK(link->forcePTOReclaimQLog(level, probes) && probes,
    "PTO qlog reclaim did not fire");
  link->forcePTOProbeQLog(level, probes);
  link->teardown();

	closeQLog(app.qlogTrace());
	ZutcpLogger::stop();
	ZutcpLogDiag diag = ZutcpLogger::diag();
	ZutcpLogger::final(app.qlogTrace());

  ZuCHECK(diag.recordsEnqueued >= 3, "PTO qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 3, "PTO qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "PTO qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "PTO qlog output was not written");
  ZuCHECK(parseJSONSeq(data) >= 4, "PTO qlog JSON-SEQ parse failed");
  ZuCHECK(data.find<"quic:packet_lost">() >= 0,
    "PTO qlog missing packet_lost");
  ZuCHECK(data.find<"quic:marked_for_retransmit">() >= 0,
    "PTO qlog missing marked_for_retransmit");
  ZuCHECK(data.find<"quic:timer_updated">() >= 0,
    "PTO qlog missing loss_timer_updated");
  ZuCHECK(data.find<"\"trigger\":\"reordering_threshold\"">() >= 0,
    "PTO qlog missing reordering-threshold loss trigger");
  ZuCHECK(data.find<"\"timer_type\":\"pto\"">() >= 0,
    "PTO qlog missing timer type");
  ZuCHECK(data.find<"\"event_type\":\"expired\"">() >= 0,
    "PTO qlog missing expired event type");
  constexpr auto Delta = "\"event_type\":\"set\",\"delta\":"_z;
  bool positive = false;
  if (int i = data.find(Delta); i >= 0) {
    unsigned start = unsigned(i) + Delta.length();
    positive = double(ZuBox<double>{ZuCSpan{
      data.data() + start, data.length() - start}}) > 0;
  }
  ZuCHECK(positive,
    "transport timer must retain a positive qlog duration");
}

#endif

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
  auto cxn = testLink(&app);
  ZmRef<MxRxCxn> mx = new MxRxCxn{app.mx()};
  RxAttachAlloc allocator;
  unsigned calls = 0, depth = 0, maxDepth = 0;
  bool exact = true;
  ZmRef<ZiIOBuf> held;
  ZmBlock<>{}([&app, cxn, mx, &allocator, &calls, &depth, &maxDepth,
      &exact, &held, reentrant](auto wake) mutable {
    app.mx()->run([cxn, mx, &allocator, &calls, &depth, &maxDepth,
	&exact, &held, reentrant, wake = ZuMv(wake)]() mutable {
      cxn->mxCxn = mx;
      mx->plugin(static_cast<TestLink::Base *>(cxn.ptr()));
      auto &context = mx->rxContext();
      context.buf = allocator.allocBuf(cxn.ptr());
      Zutcp::Frame frame;
      unsigned used = 0;
      auto packet = dataPkt(5, "world", false, frame, used);
      exact &= cxn->processFrame(frame, packet) >= 0 && calls == 0 &&
	!context.buf->length;
      packet = dataPkt(0, "hello", false, frame, used);
      exact &= cxn->processFrame(frame, packet) == 0 && calls == 0 &&
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
	    static_cast<TestLink::Base *>(context.cxn->plugin())->recvMx_();
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
      exact &= static_cast<TestLink::Base *>(mx->plugin())->recvMx_() == 1 &&
	calls == 1;
      // True stops this delivery turn. A later recv notification resumes the
      // queued suffix and re-reads the current stub (or app's partial frame).
      if (!reentrant) cxn->recvMx_();
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
  cxn->teardown();
}

void testMxTxQueue()
{
  ZuTestScope(testMxTxQueue);
  App app;
  auto cxn = testLink(&app);
  ZmRef<MxRxCxn> mx = new MxRxCxn{app.mx()};
  cxn->mxCxn = mx;
  mx->plugin(static_cast<TestLink::Base *>(cxn.ptr()));
  bool pending = false, resumed = false, exact = true, zero = false;
  unsigned calls = 0;
  app.onTx([cxn, mx, &pending, &resumed, &exact, &zero, &calls] {
    cxn->txQueueMax(1);
    auto first = Zutcp::inlineAllocator()->allocBuf();
    auto second = Zutcp::inlineAllocator()->allocBuf();
    *first << "one";
    *second << "xtwo";
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
      context.init(ZuMv(complete), ZuMv(next));
      context.offset = 1;
      target->sendMx_(); // same-owner reentry must defer behind this completion
      return true;
    }}, first);
    cxn->sendMx_();
    pending = calls == 1 && !context.completed() && context.buf == second &&
      context.offset == 1 && !context.length && cxn->txQueueCount() == 1;
    cxn->dequeued = [native = cxn.ptr()] { native->sendMx_(); };
    Zutcp::TxRange sent;
    bool dequeued = cxn->dequeueTxRange(sent);
    Zutcp::TxRange next;
    resumed = dequeued && calls == 2 && context.completed() && cxn->txQueueCount() == 1 &&
      cxn->txRange(0, next) && next.length == 3 && next.dataOffset == 3 &&
      !memcmp(next.storage->data + next.offset, "two", 3) &&
      mx->txBytes() == 6 && mx->txCalls() == 2;
    auto empty = Zutcp::inlineAllocator()->allocBuf();
    context.init(ZiTxFn{[&exact](ZiTxContext &context) {
      exact &= !context.length;
      context.complete();
      return true;
    }}, empty);
    cxn->sendMx_();
    zero = context.completed() && mx->txBytes() == 6 && mx->txCalls() == 2;
    cxn->dequeued = {};
  });
  ZuCHECK(pending, "a full native Tx queue must leave the replacement context pending");
  ZuCHECK(resumed && exact,
    "dequeue credit must resume exact offset bytes with local result counts");
  ZuCHECK(zero, "zero-byte context completion must not create stream progress counts");
  cxn->mxCxn = nullptr;
  mx->plugin(nullptr);
  cxn->teardown();
}


int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("ZutcpDataTest");
#ifdef Zutcp_DEBUG
  g_residue = ZiTestResidue::dir("qlog");
#endif
  ZuTestMain();
  ZuTestCall(testDataFrameDelivery);
  ZuTestCall(testDataRxSliceDelivery);
  ZuTestCall(testMxRxStub, false, false);
  ZuTestCall(testMxRxStub, true, false);
  ZuTestCall(testMxRxStub, false, true);
  ZuTestCall(testMxRxDelivery, false);
  ZuTestCall(testMxRxDelivery, true);
  ZuTestCall(testMxTxQueue);
  ZuTestCall(testOutOfOrderDataDelivery);
  ZuTestCall(testRxRetirement);
  ZuTestCall(testDataTxRetention);
  ZuTestCall(testDataTxRetainedBuffers);
  ZuTestCall(testDataTxQueueLimit);
  ZuTestCall(testFinAdmissionHandoff);
  ZuTestCall(testAbortAdmissionHandoff);
  ZuTestCall(testDataTxUnackd);
  ZuTestCall(testDataTxUnackdFin);
  ZuTestCall(testLinkDataTxUnackdAck);
  ZuTestCall(cumulativeAckAuthority);
  ZuTestCall(byteAckRTT);
  ZuTestCall(registeredTxBoundary);
  ZuTestCall(registeredCoveringRetx, false);
  ZuTestCall(registeredCoveringRetx, true);
  ZuTestCall(dataRetxUnackdIdempotent);
  ZuTestCall(testDataPktizer);
  ZuTestCall(testDataProducerCredit);
  ZuTestCall(testDataRetxPacketBudget);
  ZuTestCall(testCtlSendFailRetainsFrame);
  ZuTestCall(testMultiFrameAssembly);
  ZuTestCall(cumulativeAckRetxSuffix);
  ZuTestCall(testDataRetxClipsUnackdFin);
  ZuTestCall(testPktNumLength);
  ZuTestCall(congestionBudgetGatesSends);
  ZuTestCall(ackCreditServicesRetx, true);
  ZuTestCall(ackCreditServicesRetx, false);
  ZuTestCall(testDataBatchContinuation);
  ZuTestCall(testActivePathBudget);
#ifdef Zutcp_DEBUG
  ZuTestCall(testBootstrapCIDRoutes);
  ZuTestCall(testPMTUDQLog);
  ZuTestCall(testAckECNValidateDisable);
  ZuTestCall(testRxQLog);
  ZuTestCall(testFlowControlQLog);
  ZuTestCall(testPTOQLog);
#endif
  ZuTestCall(testBlockedFrameDedup);
  ZuTestCall(testKeyedControlReplacement);
  ZuTestCall(testControlInvalidation);
  ZuTestCall(testResetStopFrames);
  ZuTestCall(testLocalResetStopSend);
  ZuTestCall(tcpWindowState);
  ZuTestCall(tcpSegmentAdmission);
  ZuTestCall(tcpSegmentEdges);
  ZuTestCall(tcpNativeReset);
  ZuTestCall(tcpLocalAbort);
  ZuTestCall(tcpNativeFinish);
  ZuTestCall(tcpNativeHistorySnapshot);
  ZuTestCall(tcpOriginWrap);
  ZuTestCall(positiveWindowPacket, false);
  ZuTestCall(positiveWindowPacket, true);
  ZuTestCall(positiveWindowReceive);
  ZuTestCall(tcpFinWindow);
  ZuTestCall(tcpReceiveWindow);
  ZuTestCall(windowPersist);
  ZuTestCall(windowUpdateGeneration);
  ZuTestCall(windowProbeReply);
  ZuTestCall(testMaxBlockedValidate);
  ZuTestCall(frameRoleAndSpaceLegality);
#ifdef Zutcp_DEBUG
#endif
}
