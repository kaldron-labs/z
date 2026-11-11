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
#include <zlib/Zquic.hh>

#include "ZiTestResidue.hh"

#include <zpicotls/openssl.h>

using namespace ZuTestUtil;

#define ZQUIC_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

using StreamTxBufAlloc = Zquic::StreamTxBufAlloc<>;

static ZuBSpan span_(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
}

#ifdef Zquic_DEBUG
static Zi::Path g_residue;

static void closeQLog(ZquicLogger::Trace &trace)
{
  ZmBlock<>{}([&trace](auto wake) {
    ZquicLogger::close(trace, ZuMv(wake));
  });
}
#endif

struct App : public Zquic::Hub<App> {
  using Base = Zquic::Hub<App>;

  App(unsigned maxUDP = Zquic::MinUDPPayload, uint64_t rxWindow = 0) :
    m_mx{mxParams_()} {
    auto params = Zquic::HubParams(&m_mx, "3", "4");
    params.maxUDP(maxUDP);
    if (rxWindow) params.maxData(rxWindow).maxStreamData(rxWindow);
    bool ok = Base::init(params);
    ZiAssert(ok, "Zquic", (), "stream test app init failed", return);
    ZiAssert(m_mx.start(), "Zquic", (),
      "stream test multiplexer start failed", return);
  }
  ~App() {
    Base::final();
    m_mx.stop();
  }

  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
  template <typename L> void rxRun(L l) { l(); }
  template <typename L> void rxInvoke(L l) { l(); }
  template <typename L> void txRun(L l) {
    if (deferredTx) m_mx.run(ZuMv(l), Base::txThread());
    else l();
  }
  template <typename L> void txInvoke(L l) { l(); }
  template <typename O, typename L> void txInvoke(O *, L l) { l(); }
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
  template <typename Link>
  bool allowEarlyStream(Link *, uint64_t streamID, bool fin) {
    ++earlyStreamChecks;
    lastEarlyStreamID = streamID;
    lastEarlyStreamFin = fin;
    return earlyStreamAllowed;
  }

  bool		earlyStreamAllowed = false;
  bool		deferredTx = false;
  unsigned	earlyStreamChecks = 0;
  uint64_t	lastEarlyStreamID = 0;
  bool		lastEarlyStreamFin = false;

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
struct TestStream :
  public Zquic::Stream<TestLink, TestStream, StreamTxBufAlloc>
{
  using Base = Zquic::Stream<TestLink, TestStream, StreamTxBufAlloc>;
  using Base::Base;
  int process(Zquic::RxStream &) { ++processed; return 0; }
  void txDequeued_() { if (dequeued) dequeued(); }

  unsigned processed = 0;
  ZmFn<void()> dequeued;
};
struct TestCIDRoutes {
  bool add(
    const Zquic::CxnID &, uint64_t, TestLink *,
    const Zquic::ResetToken & = {}) {
    ++adds;
    return true;
  }
  bool retire(const Zquic::CxnID &) {
    ++retires;
    return true;
  }
  bool tombstone(const Zquic::CxnID &) {
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
  public Zquic::Link<App, TestLink,
    StreamTxBufAlloc, TestStream>
{
friend inline ZuStringT<"Zquic.StreamTest.Link"> ZmNHeapID(TestLink *);

  using Base = Zquic::Link<App, TestLink,
    StreamTxBufAlloc, TestStream>;
  friend inline ZuStringT<"Zquic.StreamTest.Link"> ZmNHeapID(TestLink *);
  using ZmNHeap<TestLink>::operator new;
  using ZmNHeap<TestLink>::operator delete;

  RecoveryService *service = nullptr;
  unsigned *probeSpaces = nullptr;
  bool rejectProbe = false;
  Zquic::PktNumSpace::T rejectProbeSpace = Zquic::PktNumSpace::N;
  bool recordRejectedProbe = false;
  unsigned rejectedProbeRecords = 0;

  TestLink(App *app, bool isServer = false) : Base{app, isServer} {
    Base::configLocalParams(app);
  }
  void resetRuntimeForTest() {
    Base::resetLink_();
    Base::configLocalParams(app());
  }
  void closeForTest(uint64_t errorCode = 0) {
    Base::closeState(errorCode);
    teardown();
  }
  void streamed(ZmRef<TestStream> stream) {
    lastStream = ZuMv(stream);
    ++streamedCount;
  }
  bool queuePathResponse() {
    return Base::queuePathResponse_(ZuBSpan{"response"});
  }
  bool queuePathResponse(unsigned seed) {
    uint8_t data[8]{};
    data[0] = uint8_t(seed);
    return Base::queuePathResponse_(ZuBSpan{data, sizeof(data)});
  }
  bool queueDataBlocked(uint64_t value) {
    return Base::queueBlocked(Zquic::FrameType::DataBlocked, 0, value);
  }
  bool queueMaxData(uint64_t value) {
    return Base::txQueueControl_(Zquic::ControlFrame::flowUpdate(
      Zquic::FlowUpdate{
	Zquic::FrameType::MaxData, 0, value, Zquic::StreamType::Duplex}));
  }
  bool queueMaxStreamData(uint64_t streamID, uint64_t value) {
    return Base::txQueueControl_(Zquic::ControlFrame::flowUpdate(
      Zquic::FlowUpdate{
	Zquic::FrameType::MaxStreamData, streamID, value,
	Zquic::StreamType::Duplex}));
  }
  bool queueDataBlocked(uint64_t streamID, uint64_t value) {
    return Base::queueBlocked(
      Zquic::FrameType::StreamDataBlocked, streamID, value);
  }
  bool queueStreamsBlocked(Zquic::StreamType::T type, uint64_t value) {
    return Base::queueBlocked(
      Zquic::FrameType::StreamsBlocked, 0, value, type);
  }
  void scheduleStream(const ZmRef<TestStream> &stream) {
    Base::streamWritable(stream);
  }
  unsigned scheduledStreams() const { return Base::scheduledStreamCount(); }
  void grantDataCredit(uint64_t value) { Base::txApplyMaxData_(value); }
  void grantStreamCredit(const ZmRef<TestStream> &stream, uint64_t value) {
    Base::txApplyMaxStreamData_(stream, value);
  }
  bool applyMaxStreams(const Zquic::Frame &frame) {
    return frame.type == Zquic::FrameType::MaxStreams &&
      Base::txApplyMaxStreams_(frame.streamType, frame.value);
  }
  bool flushControlSendFails() {
    return Base::flushCtlStreams(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [](Zquic::PktBuild &, ZiSockAddr, const Base::TxPktRefs &) {
	return false;
      });
  }
  bool flushControlSends() {
    return Base::flushCtlStreams(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [](Zquic::PktBuild &, ZiSockAddr, const Base::TxPktRefs &) {
	return true;
      });
  }
  bool flushStreamBatch() {
    return Base::flushCtlStreamsBatch_(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [this](Zquic::PktBuild &build, ZiSockAddr, const Base::TxPktRefs &refs) {
	bool ok = Base::recordProtPktTx(
	  Zquic::PktNumSpace::AppData, sentPkts, build.bytes(), {}, &refs, true);
	if (ok) ++sentPkts;
	return ok;
      });
  }
  unsigned flushFrameRefs(
    Zquic::SentFrameKind::T *kinds, uint64_t *streamIDs,
    unsigned capacity) {
    unsigned refs = 0;
    bool ok = Base::flushCtlStreams(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [&refs, &kinds, &streamIDs, capacity](
	  Zquic::PktBuild &, ZiSockAddr, const Base::TxPktRefs &r) {
	refs = r.count();
	for (unsigned i = 0; i < refs && i < capacity; ++i) {
	  kinds[i] = r[i].kind;
	  streamIDs[i] = r[i].streamID;
	}
	return true;
      });
    return ok ? refs : 0;
  }
  unsigned flushSentRefs(Zquic::SentFrameRef *refs, unsigned capacity) {
    unsigned n = 0;
    bool ok = Base::flushCtlStreams(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [&n, refs, capacity](
	  Zquic::PktBuild &, ZiSockAddr, const Base::TxPktRefs &r) {
	n = r.count();
	for (unsigned i = 0; i < n && i < capacity; ++i) refs[i] = r[i];
	return true;
      });
    return ok ? n : 0;
  }
  void recordSentPkt(
    Zquic::PktNumSpace::T level, uint64_t pn, unsigned bytes,
    const Zquic::SentFrameRef &ref, bool ackEliciting,
    Zquic::PktType::T packetType = Zquic::PktType::N,
    Zquic::EcnMark::T ecn = Zquic::EcnMark::NotECT) {
    Base::recordTxPkt(
      level, pn, bytes, ref, ackEliciting, false, 0,
      Zquic::PktNumSpace::N, 0, packetType, ecn);
    Base::setTxPN(level, pn + 1);
  }
  void recordSentPkt(
    Zquic::PktNumSpace::T level, uint64_t pn, unsigned bytes,
    const Base::TxPktRefs &refs, bool ackEliciting,
    Zquic::PktType::T packetType = Zquic::PktType::N) {
    Base::recordTxPkt(
      level, pn, bytes, refs, ackEliciting, false, 0,
      Zquic::PktNumSpace::N, 0, packetType);
    Base::setTxPN(level, pn + 1);
  }
  unsigned flushRecordedRefs(Zquic::SentFrameRef *refs, unsigned capacity) {
    unsigned n = 0;
    bool ok = Base::flushCtlStreams(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [this, &n, refs, capacity](Zquic::PktBuild &build, ZiSockAddr,
	  const Base::TxPktRefs &r) {
	n = r.count();
	for (unsigned i = 0; i < n && i < capacity; ++i) refs[i] = r[i];
	recordSentPkt(
	  Zquic::PktNumSpace::AppData, sentPkts, build.bytes(), r, true);
	++sentPkts;
	return true;
      });
    return ok ? n : 0;
  }
  bool rebuildControl(
    const Zquic::SentFrameRef &ref, Zquic::Frame &frame,
    Zquic::PathData *payload = nullptr) {
    Zquic::PktBuild build;
    if (!Base::buildRetxCtl(build, ref) || !build.count())
      return false;
    unsigned used = 0;
    bool parsed = !Zquic::FrameCodec::parse(
      ZuBSpan{build.data()[0].base, unsigned(build.data()[0].len)},
      frame, used) && used == build.data()[0].len;
    if (parsed && payload) *payload = frame.payload;
    return parsed;
  }
  bool flushCongestedStream(ZmRef<TestStream> stream) {
    return Base::sendQueuedStreamPkt(
      ZuMv(stream), ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [this](
	  Zquic::PktBuild &build, ZiSockAddr,
	  const Base::TxPktRefs &refs) {
	recordSentPkt(
	  Zquic::PktNumSpace::AppData, sentPkts, build.bytes(), refs, true);
	++sentPkts;
	return true;
      });
  }
  void fillCwnd() {
    Zquic::LinkDiag diag = Base::diag_();
    while (diag.tx.bytesInFlight < diag.tx.congestionWindow) {
      uint64_t remaining =
	diag.tx.congestionWindow - diag.tx.bytesInFlight;
      unsigned bytes = remaining > app()->maxUDP() ?
	app()->maxUDP() : unsigned(remaining);
      recordSentPkt(
	Zquic::PktNumSpace::AppData, sentPkts, bytes,
	Zquic::SentFrameRef::control(), true);
      ++sentPkts;
      diag = Base::diag_();
    }
  }
  void ackThrough(uint64_t pn) {
    Base::AckSnapshot ack;
    ack.level = Zquic::PktNumSpace::AppData;
    new (ack.ranges.push()) Zquic::AckRange{pn, 0};
    Base::processAckFrameTx_(ack);
  }
  void ackRange(uint64_t first, uint64_t largest) {
    Base::AckSnapshot ack;
    ack.level = Zquic::PktNumSpace::AppData;
    new (ack.ranges.push()) Zquic::AckRange{largest, first};
    Base::processAckFrameTx_(ack);
  }
  void ackOnly(
    uint64_t pn, Zquic::PktNumSpace::T level = Zquic::PktNumSpace::AppData) {
    Base::AckSnapshot ack;
    ack.level = level;
    new (ack.ranges.push()) Zquic::AckRange{pn, pn};
    Base::processAckFrameTx_(ack);
  }
	  bool nextRetransmitRef(
	    Zquic::PktNumSpace::T &level, Zquic::SentFrameRef &ref,
	    Zquic::PktNumSpace::T onlySpace = Zquic::PktNumSpace::N) {
	    return Base::nextRetransmit_(level, ref, onlySpace);
	  }
	  bool forcePTOReclaimQLog(
	    Zquic::PktNumSpace::T &level, unsigned &probes) {
	    Base::notePTOExpired();
	    return Base::reclaimPTO(level, probes);
	  }
	  void forcePTOProbeQLog(Zquic::PktNumSpace::T level, unsigned probes) {
	    Base::notePTOProbe(level, probes);
	  }
  bool sendCryptoBytes(
    ZuBSpan data, Zquic::PktNumSpace::T level = Zquic::PktNumSpace::AppData) {
    size_t offsets[5] = {0, 0, 0, 0, data.length()};
    if (level == Zquic::PktNumSpace::Initial)
      for (unsigned i = 1; i < 4; ++i) offsets[i] = data.length();
    if (level == Zquic::PktNumSpace::Handshake) offsets[3] = data.length();
    ZmRef<ZiIOBuf> backing = app()->cryptoTxAllocator()->allocBuf();
    backing->append(data);
    Zquic::CryptoOffsets positions(Zquic::TLSEpochCount + 1, false);
    for (unsigned i = 0; i <= Zquic::TLSEpochCount; ++i) positions[i] = offsets[i];
    return Base::sendCryptoFlights_(
      ZuMv(backing), positions, 900, ZiSockAddr{},
      [this](
	  Zquic::PktNumSpace::T level, ZuBSpan prefix, ZuBSpan payload,
	  const Zquic::SentFrameRef &ref, ZiSockAddr) {
	recordSentPkt(
	  level, sentPkts, prefix.length() + payload.length(), ref, true);
	++sentPkts;
	return true;
      });
  }
  bool installHandshake(const Zquic::TrafficSecret &secret) {
    return Base::txInstallSecret(Zquic::PktNumSpace::Handshake, secret);
  }
  bool installAppTx(const Zquic::TrafficSecret &secret) {
    return Base::txInstallSecret(Zquic::PktNumSpace::AppData, secret);
  }
  void confirmHandshake() {
    Base::discardPktNumSpace_1(Zquic::PktNumSpace::Handshake);
  }
  bool recordProbePkt(
    Zquic::PktNumSpace::T level, unsigned bytes,
    const Zquic::SentFrameRef &ref) {
    Base::TxPktRefs refs;
    refs.add(ref);
    uint64_t pn = sentPkts++;
    Base::setTxPN(level, pn);
    return Base::recordProtPktTx(level, pn, bytes, {}, &refs, true);
  }
  void probePTO(
    unsigned *spaces, bool reject = false,
    Zquic::PktNumSpace::T rejectSpace = Zquic::PktNumSpace::N,
    bool recordRejected = false) {
    probeSpaces = spaces;
    rejectProbe = reject;
    rejectProbeSpace = rejectSpace;
    recordRejectedProbe = recordRejected;
    rejectedProbeRecords = 0;
    Base::ptoRecovery(
      [this, spaces, reject, rejectSpace](Zquic::PktNumSpace::T level,
	  Zquic::PktBuild &build, const Zquic::SentFrameRef &ref) {
	if (reject || level == rejectSpace) return false;
	if (!recordProbePkt(level, build.bytes(), ref)) return false;
	++spaces[level];
	return true;
      },
      []() { return false; },
      [this, spaces, reject, rejectSpace](Zquic::PktNumSpace::T level) {
	if (reject || level == rejectSpace) return false;
	Zquic::PktBuild build;
	if (!Base::buildPingProbe(level, build)) return false;
	if (!recordProbePkt(level, build.bytes(),
	    Zquic::SentFrameRef::control()))
	  return false;
	++spaces[level];
	return true;
      });
    probeSpaces = nullptr;
  }
  bool nextCryptoPTO(
    Zquic::PktNumSpace::T &level, Zquic::SentFrameRef &ref) {
    return Base::nextCryptoPTO_(level, ref);
  }
  bool sendRetxCrypto(
    Zquic::PktNumSpace::T level, Zquic::SentFrameRef &ref,
    unsigned &bytes, uint64_t &pn, bool reject = false) {
    Zquic::PktBuild build;
    if (!Base::buildRetxCrypto(level, build, ref)) return false;
    Base::TxPktRefs refs;
    refs.add(ref);
    pn = sentPkts++;
    Base::setTxPN(level, pn);
    auto alloc = []() { return ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return Zquic::pktTxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr); };
    auto send = [this, &bytes, reject](auto buf, ZiSockAddr addr) {
      bytes = buf->length;
      if (reject) return false;
      return Base::sendPathPkt(
	ZuMv(buf), ZuMv(addr),
	[](ZmRef<ZiIOBuf>, ZiSockAddr, Zquic::EcnMark::T) { return true; });
    };
    return Base::sendProtHandshakePkt(
      Base::CIDSel::Peer, Base::CIDSel::Local, Base::txPNLength(level),
      build, ZiSockAddr{}, {}, &refs, true, alloc, send);
  }

  void sendCryptoRef(
    uint64_t pn, Zquic::PktNumSpace::T level,
    uint64_t offset, uint64_t length) {
    recordSentPkt(
      level, pn, 100, Zquic::SentFrameRef::crypto(offset, length), true);
  }
  void sendControlRef(uint64_t pn, const Zquic::SentFrameRef &ref) {
    recordSentPkt(
      Zquic::PktNumSpace::AppData, pn, 100, ref, true);
  }
  bool rebuildCrypto(
    Zquic::PktNumSpace::T level, Zquic::SentFrameRef ref,
    Zquic::Frame &frame, uint8_t *b, unsigned size) {
    Zquic::PktBuild build;
    if (!Base::buildRetxCrypto(level, build, ref)) return false;
    unsigned n = 0;
    for (unsigned i = 0, l = build.count(); i < l; ++i) {
      if (build.data()[i].len > size - n) return false;
      memcpy(b + n, build.data()[i].base, build.data()[i].len);
      n += unsigned(build.data()[i].len);
    }
    unsigned used = 0;
    return !Zquic::FrameCodec::parse(
      ZuBSpan{b, n}, frame, used) &&
      used == n;
  }
  void teardown() {
    ZmBlock<>{}([this](auto wake) {
      Base::disconnect_(false, ZuMv(wake));
    });
    app()->drainTx();
  }
  void sendStreamRef(
    uint64_t pn, const ZmRef<TestStream> &stream,
    uint64_t offset, uint64_t length, bool fin = false) {
    Zquic::TxRange range{nullptr, 0, uint32_t(length), offset};
    Zquic::SentFrameRef ref =
      Zquic::SentFrameRef::stream(uint64_t(stream->id()), range, fin);
    if (!length) {
      ref.offset = offset;
      ref.length = 0;
    }
    recordSentPkt(
      Zquic::PktNumSpace::AppData, pn, 100, ref, true);
  }
  void advancePN(unsigned n) {
    for (unsigned i = 0; i < n; ++i)
      Base::recordProtPktTx(
	Zquic::PktNumSpace::AppData, i, 1, {}, nullptr, false);
  }
#ifdef Zquic_DEBUG
  void forcePN(uint64_t pn) {
    Base::setTxPN(Zquic::PktNumSpace::AppData, pn);
  }
  bool addLocalCID(
    const Zquic::CxnID &id, uint64_t sequence,
    const Zquic::ResetToken &token = {}) {
    return Base::addLocalCID_(id, sequence, token);
  }
  bool rxNewCxnID(const Zquic::Frame &frame) {
    return Base::receiveNewCxnID_(frame);
  }
  bool rxRetireCxnID(const Zquic::Frame &frame) {
    return Base::receiveRetireCxnID_(frame);
  }
  void installLocalCIDRoutes(TestCIDRoutes &routes) {
    Base::installLocalCIDRoutes_(routes);
  }
  void retireLocalCIDRoutes(TestCIDRoutes &routes) {
    Base::retireLocalCIDRoutes_(routes);
  }
  void tombstoneLocalCIDRoutes(TestCIDRoutes &routes) {
    Base::tombstoneLocalCIDRoutes_(routes);
  }
#endif
  unsigned pnLength() const {
    return Base::txPNLength(Zquic::PktNumSpace::AppData);
  }
  bool coalesceProbe(unsigned &sends, unsigned &bytes) {
    sends = bytes = 0;
    ZmRef<ZiIOBuf> initial = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return Zquic::pktTxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
    ZmRef<ZiIOBuf> handshake = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return Zquic::pktTxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
    if (ZuUnlikely(!initial || !handshake)) return false;
    initial->skip = handshake->skip = 0;
    initial->length = 100;
    handshake->length = 40;
    memset(initial->data_(), 0x11, initial->length);
    memset(handshake->data_(), 0x22, handshake->length);
    Base::beginLongCoalesce();
    bool ok = Base::holdInitialForCoalesce(
      ZuMv(initial), ZiSockAddr{},
      [&sends, &bytes](auto buf, ZiSockAddr) {
	++sends;
	bytes += buf->length;
	return true;
      });
    ok = Base::sendHandshakeCoalesced(
      ZuMv(handshake), ZiSockAddr{},
      [&sends, &bytes](auto buf, ZiSockAddr) {
	++sends;
	bytes += buf->length;
	return buf->length == 140 &&
	  buf->data_()[99] == 0x11 &&
	  buf->data_()[100] == 0x22 &&
	  buf->data_()[139] == 0x22;
      }) && ok;
    Base::endLongCoalesce();
    return ok;
  }
  void sendAckEliciting(uint64_t pn, unsigned bytes = 1200) {
    recordSentPkt(
      Zquic::PktNumSpace::AppData, pn, bytes,
      Zquic::SentFrameRef::control(), true);
  }
  void sendECN(
    uint64_t pn, Zquic::EcnMark::T ecn, unsigned bytes = 1200) {
    recordSentPkt(
      Zquic::PktNumSpace::AppData, pn, bytes,
      Zquic::SentFrameRef::control(), true, Zquic::PktType::N, ecn);
  }
#ifdef Zquic_DEBUG
  void ackECN(uint64_t largest, uint64_t ect0, uint64_t ect1, uint64_t ce) {
    Base::setTxPN(Zquic::PktNumSpace::AppData, largest + 1);
    Base::AckSnapshot ack;
    ack.level = Zquic::PktNumSpace::AppData;
    new (ack.ranges.push()) Zquic::AckRange{largest, 0};
    ack.ecn.ect0 = ect0;
    ack.ecn.ect1 = ect1;
    ack.ecn.ce = ce;
    Base::processAckFrameTx_(ack);
  }
  void ackNoECN(uint64_t largest) {
    Base::setTxPN(Zquic::PktNumSpace::AppData, largest + 1);
    Base::AckSnapshot ack;
    ack.level = Zquic::PktNumSpace::AppData;
    new (ack.ranges.push()) Zquic::AckRange{largest, 0};
    Base::processAckFrameTx_(ack);
  }
#endif
  bool receiveMarked(
    uint64_t pn, Zquic::EcnMark::T ecn, bool ackEliciting = true,
    Zquic::PktNumSpace::T level = Zquic::PktNumSpace::AppData) {
    if (Base::rxPktSeen(level, pn)) return false;
    Base::noteAck(level, pn, ackEliciting, ZiSockAddr{}, false, ecn);
    return true;
  }
  bool writePendingAck(Zquic::PktBuild &build) {
    return Base::appendPendingAck(Zquic::PktNumSpace::AppData, build);
  }
  Zquic::PktBudget sendBudget() const { return Base::sendBudget_(); }
  void initServerPath() {
    Base::initServerPathTx_(ZiSockAddr{}, ZiSockAddr{});
  }
  void initServerPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initServerPathTx_(ZuMv(local), ZuMv(remote));
  }
#ifdef Zquic_DEBUG
  void observePath(ZiSockAddr local, ZiSockAddr remote) {
    Base::startPathValidation(ZuMv(local), ZuMv(remote));
  }
  void observeCandidate(ZiSockAddr local, ZiSockAddr remote, unsigned bytes) {
    Base::observePathRxTx_(ZuMv(local), ZuMv(remote), bytes, false);
  }
  bool candidateValidated() {
    const auto &path = Base::txPath(Base::validatingRemote());
    return path.validated();
  }
  uint64_t candidateAntiAmp() {
    return Base::txPath(Base::validatingRemote()).antiAmpRemaining();
  }
  void recoverPTO() {
    Base::ptoRecovery(
      [](auto &&...) { return false; },
      []() { return false; },
      [](Zquic::PktNumSpace::T) { return false; });
  }
  bool validatingPath() const { return Base::validatingPath_(); }
  ZuBSpan validatingChallenge() const {
    return Base::validatingChallenge_();
  }
#endif
  bool pathResponse(ZuBSpan data) { return Base::onPathResponse_(data); }
  void pathTimeout() { Base::pathExpired(); }
  const ZiSockAddr &activePathRemote() const {
    return Base::activePathRemote_();
  }
  void pathReceived(unsigned bytes) { Base::recordPathRxTx_(ZiSockAddr{}, bytes); }
  bool pathSend(unsigned bytes) {
    ZmRef<ZiIOBuf> buf = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return Zquic::pktTxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
    if (ZuUnlikely(!buf)) return false;
    buf->skip = 0;
    buf->length = bytes;
    return Base::sendPathPkt(
      ZuMv(buf), ZiSockAddr{},
      [](ZmRef<ZiIOBuf>, ZiSockAddr, Zquic::EcnMark::T) { return true; });
  }
  void validatePath() { Base::validatePathTx_(); }
  void sparseAck() {
    constexpr unsigned Ranges = 16;
    Base::AckSnapshot ack;
    ack.level = Zquic::PktNumSpace::AppData;
    ack.due = true;
    ack.largestRxTime = Base::nowUS();
    for (unsigned i = 0; i < Ranges; ++i) {
      uint64_t pn = uint64_t(i + 1) << 20;
      new (ack.ranges.push()) Zquic::AckRange{pn, pn};
    }
    Base::noteAckTx(ack);
  }
  bool ackPending() const { return Base::pendingAck(Zquic::PktNumSpace::AppData); }
  bool requeueStream(const Zquic::SentFrameRef &ref) {
    return Base::requeueStreamRef(Zquic::PktNumSpace::AppData, ref);
  }
  bool protectedStreamRetx(
    Zquic::SentFrameRef &ref, ZmRef<ZiIOBuf> &wire,
    bool fail, bool &withAck) {
    Zquic::PktBuild build;
    if (!Base::buildRetxStream(build, ref)) return false;
    withAck = build.ack(Zquic::PktNumSpace::AppData);
    uint8_t plain[Zquic::BufSize];
    unsigned bytes = 0;
    bool retained = !ref.length;
    for (unsigned i = 0; i < build.count(); ++i) {
      const auto &part = build.data()[i];
      if (part.len > sizeof(plain) - bytes) return false;
      memcpy(plain + bytes, part.base, part.len);
      bytes += unsigned(part.len);
      if (ref.range.buf && part.base == ref.range.buf->data_() + ref.range.offset &&
	  part.len == ref.length) retained = true;
    }
    bool framed = false;
    for (unsigned offset = 0; offset < bytes;) {
      Zquic::Frame frame;
      unsigned used = 0;
      if (Zquic::FrameCodec::parse(span_(plain + offset, bytes - offset), frame, used) ||
	  !used) return false;
      offset += used;
      if (frame.type != Zquic::FrameType::Stream) continue;
      if (frame.streamID != ref.streamID || frame.offset != ref.offset ||
	  frame.length != ref.length || frame.fin != ref.fin ||
	  frame.payload != span_(ref.range.buf->data_() + ref.range.offset,
	    unsigned(ref.length))) return false;
      framed = true;
    }
    if (!framed || !retained) return false;
    Base::TxPktRefs refs;
    if (!refs.add(ref)) return false;
    return Base::sendProtShortPkt(
      CIDSel::Peer, Base::txPNLength(Zquic::PktNumSpace::AppData),
      build, ZiSockAddr{}, {}, &refs, true,
      []() {
	static Zi::IOBufAllocator *allocator =
	  ([] { static ZiMultiplex mx; return Zquic::pktTxAllocator(&mx); })();
	return allocator->allocBuf(nullptr, 0);
      },
      [this, &wire, fail](auto buf, ZiSockAddr addr, Zquic::EcnMark::T ecn) {
	return Base::sendPathPkt(ZuMv(buf), ZuMv(addr),
	  [&wire, fail](auto packet, ZiSockAddr, Zquic::EcnMark::T) {
	    wire = ZuMv(packet);
	    return !fail;
	  }, ecn);
      });
  }
  bool protectedPathResponse(unsigned &wireBytes) {
    Zquic::PktBuild build;
    Base::TxPktRefs refs;
    if (!Base::buildPathResponse(ZuBSpan{"response"}, build, refs))
      return false;
    return Base::sendProtShortPkt(
      CIDSel::Peer, 1, build, ZiSockAddr{}, {}, &refs, true,
      []() {
	static Zi::IOBufAllocator *allocator =
	  ([] { static ZiMultiplex mx; return Zquic::pktTxAllocator(&mx); })();
	return allocator->allocBuf(nullptr, 0);
      },
      [this, &wireBytes](auto buf, ZiSockAddr addr, Zquic::EcnMark::T ecn) {
	return Base::sendValidPathPktApp(ZuMv(buf), ZuMv(addr),
	  [&wireBytes](auto sentBuf, ZiSockAddr, Zquic::EcnMark::T, bool &sent) {
	    wireBytes = sentBuf->length;
	    sent = true;
	    return true;
	  }, ecn);
      });
  }
#ifdef Zquic_DEBUG
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
  Zquic::PathDiag pathDiag() const { return Base::pathDiag_(); }
  bool pathValidated() const { return Base::pathValidated_(); }
  uint64_t pathAntiAmp() const {
    return Base::pathAntiAmp_();
  }
  unsigned activePathMaxUDP() const { return Base::activePathMaxUDP_(); }
  Zquic::LinkDiag diag() const { return Base::diag_(); }
  static constexpr unsigned suspiciousStreamThreshold() {
    return Base::SuspiciousStreamThreshold;
  }
  unsigned congestionAllowance() const { return Base::congestionAllowance_(); }
  bool ecnDisabled() const { return Base::ecnDisabled_(); }
  void enableECN() { Base::setEcnDisabled_(false); }
  bool ackValid(
    Zquic::PktNumSpace::T level, uint64_t first, uint64_t largest) const {
    Base::AckSnapshot ack;
    ack.level = level;
    new (ack.ranges.push()) Zquic::AckRange{largest, first};
    return Base::ackFrameValidTx(ack);
  }
  bool installOneRTT(
    const Zquic::TrafficSecret &rx, const Zquic::TrafficSecret &tx,
    const Zquic::CxnID &localCID) {
    return Base::installAppDataKeys_(rx, tx, localCID);
  }
#ifdef Zquic_DEBUG
  void discardPeerKeys() { Base::discardPeerKeys_(); }
#endif
	  bool installZeroRTTTx(const Zquic::TrafficSecret &tx) {
	    return Base::txInstallKeySecret(Zquic::PktKeyLevel::ZeroRTT, tx);
	  }
	  bool installZeroRTTRx(const Zquic::TrafficSecret &rx) {
	    return Base::rxInstallKeySecret(Zquic::PktKeyLevel::ZeroRTT, rx);
	  }
	  Zquic::LinkEarlyState::T rxEarlyState() const {
	    return Base::rxEarlyDataState();
	  }
	  Zquic::LinkEarlyState::T txEarlyState() const {
	    return Base::txEarlyDataState();
	  }
	  bool acceptZeroRTTRx() { return Base::acceptEarlyDataRx(); }
	  bool acceptZeroRTTTx() { return Base::acceptEarlyDataTx(); }
	  bool rxOneRTTSeen() const { return Base::rxOneRTTSeen_(); }
	  bool flushEarlyStreamRefs(Zquic::SentFrameRef *refs, unsigned capacity) {
    unsigned n = 0;
    bool ok = Base::flushEarlyStreams(
      ZiSockAddr{},
      [this, &n, refs, capacity](Zquic::PktBuild &build, ZiSockAddr,
	  const Base::TxPktRefs &r) {
	n = r.count();
	for (unsigned i = 0; i < n && i < capacity; ++i) refs[i] = r[i];
	recordSentPkt(
	  Zquic::PktNumSpace::AppData, sentPkts, build.bytes(), r, true,
	  Zquic::PktType::ZeroRTT);
	++sentPkts;
	return true;
      });
    return ok ? n : 0;
  }
  unsigned rejectZeroRTT() {
    return Base::rejectZeroRTTTx(Zquic::ZeroRTTReason::TLSRejected);
  }
  bool canTxZeroRTT() const { return Base::canTxZeroRTT_(); }
  bool sendZeroRTTProbe(ZmRef<ZiIOBuf> &sent) {
    uint8_t ping[1];
    int n = Zquic::FrameCodec::writePing(ping, sizeof(ping));
    if (n != 1) return false;
    Zquic::PktBuild payload;
    if (!payload.add(span_(ping, unsigned(n)))) return false;
    return Base::sendProtZeroRTTPkt(
      Base::CIDSel::Peer, Base::CIDSel::Local,
      Base::txPNLength(Zquic::PktNumSpace::AppData),
      payload, ZiSockAddr{}, span_(ping, unsigned(n)), nullptr, true,
      []() { return ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return Zquic::pktTxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr); },
      [&sent](auto buf, ZiSockAddr) {
	sent = ZuMv(buf);
	return true;
      });
  }
	  bool receiveShort(ZmRef<ZiIOBuf> buf, bool *qlogSeen = nullptr) {
    if (!buf) return false;
    Zquic::Datagram d;
    d.buf = ZuMv(buf);
    return Base::rxProtShortPkt(
      d, 0, d.buf->length,
      [this, qlogSeen](
	  Zquic::PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
		  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
		  Base::RxAckMeta &ack, auto *qlog) {
	if (qlogSeen) *qlogSeen = qlog != nullptr;
	return Base::consumeProtFrames(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog,
	  [](size_t, ZuBSpan, ZiSockAddr) { return true; },
	  [](Zquic::PktNumSpace::T, const Zquic::Frame &, ZiSockAddr) {
	    return true;
	  });
	      });
	  }
	  bool receiveZeroRTT(ZmRef<ZiIOBuf> buf) {
	    if (!buf) return false;
	    Zquic::Datagram d;
	    d.buf = ZuMv(buf);
	    return Base::receiveProtLongPkt(
	      Base::InitialKeyDir::Server, d, 0, d.buf->length,
	      [](const Zquic::LongHdr &, Zquic::Datagram &) { return true; },
	      [this](
		  Zquic::PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
		  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
		  Base::RxAckMeta &ack, auto *qlog, bool earlyData) {
		return Base::consumeProtFrames(
		  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog,
		  [](size_t, ZuBSpan, ZiSockAddr) { return true; },
		  [](Zquic::PktNumSpace::T, const Zquic::Frame &, ZiSockAddr) {
		    return true;
		  },
		  earlyData);
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
    Zquic::PktNumSpace::T onlySpace = Zquic::PktNumSpace::N) {
    ++retransmits;
    ++retransmitCalls;
    if (pto && probeSpaces) {
      Zquic::PktNumSpace::T level;
      Zquic::SentFrameRef ref;
      unsigned sent = 0;
      while (sent < maxPackets &&
	  Base::nextRetransmit_(level, ref, onlySpace)) {
	Zquic::PktBuild build;
	if (ref.kind == Zquic::SentFrameKind::Crypto &&
	    !Base::buildRetxCrypto(level, build, ref))
	  continue;
	if (rejectProbe || level == rejectProbeSpace) {
	  if (recordRejectedProbe && !rejectedProbeRecords) {
	    uint64_t pn = sentPkts;
	    if (recordProbePkt(level, build.bytes(), ref)) {
	      Base::discardProtPktTx(level, pn, build.bytes(), true);
	      ++rejectedProbeRecords;
	    }
	  }
	  Base::requeueRetransmit_(level, ref);
	  break;
	}
	if (!recordProbePkt(level, build.bytes(), ref)) {
	  Base::requeueRetransmit_(level, ref);
	  break;
	}
	++probeSpaces[level];
	++sent;
      }
      return sent;
    }
    if (!service) return false;
    service->valid &= app()->mx()->invoked(app()->txThread());
    Zquic::PktNumSpace::T level;
    Zquic::SentFrameRef ref;
    if (!Base::nextRetransmit_(level, ref)) return false;
    if (!pto && !Base::congestionAllowance_()) {
      ++service->blocked;
      Base::requeueRetransmit_(level, ref);
      return false;
    }
    service->valid &= ref.kind == Zquic::SentFrameKind::Stream &&
      ref.range.buf.ptr() == service->origin && ref.length == 8 &&
      !memcmp(ref.range.buf->data_() + ref.range.offset, "retained", 8);
    service->note('R');
    service->recoveredPN = sentPkts;
    recordSentPkt(level, sentPkts++, 100, ref, true);
    ++service->recovered;
    return true;
  }
  bool sendPassiveMigChal() {
    if (!directPassive) return false;
    Zquic::PktBuild build;
    Base::TxPktRefs refs;
    Zquic::ControlFrame frame;
    if (!Base::buildPassiveMigChal(build, refs, frame)) return false;
    passiveRemote = Base::validatingRemote();
    if (!Base::txPath(passiveRemote).reserveSend(44)) return false;
    passiveData = frame.payload;
    recordSentPkt(Zquic::PktNumSpace::AppData, sentPkts++, 44, refs, true);
    Base::passiveMigChalSent(frame);
    ++passiveSends;
    return true;
  }

  ZmRef<TestStream>	lastStream;
  unsigned		streamedCount = 0;
  uint64_t		sentPkts = 0;
  unsigned		txFlushQueued = 0;
  unsigned		ptos = 0;
  unsigned		retransmits = 0;
  unsigned		retransmitCalls = 0;
  bool			directPassive = false;
  unsigned		passiveSends = 0;
  ZiSockAddr		passiveRemote;
  Zquic::PathData	passiveData;
};

static ZmRef<TestLink> testLink(App *app, bool isServer = false)
{
  ZmRef<TestLink> link = new TestLink{app, isServer};
  link->resetRuntimeForTest();
  return link;
}

static bool trafficSecret(
  Zquic::TrafficSecret &secret, uint8_t seed)
{
  uint8_t bytes[32];
  for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = seed + i;
  return Zquic::PktProt::deriveSecret(
    secret, &ptls_openssl_aes128gcmsha256, span_(bytes, sizeof(bytes)));
}

#ifdef Zquic_DEBUG
static ZmRef<ZiIOBuf> shortPing(
  const Zquic::CxnID &dcid, const Zquic::TrafficSecret &secret,
  uint64_t pn, bool keyPhase)
{
  Zquic::PktProtState tx;
  if (!tx.init(secret, Zquic::PktNumSpace::AppData, true)) return {};
  ZmRef<ZiIOBuf> buf = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return Zquic::pktTxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
  if (ZuUnlikely(!buf)) return {};
  enum { PNLength = 2 };
  int h = Zquic::Pkt::writeShort(
    buf->data_(), buf->size, dcid, pn, PNLength, keyPhase);
  if (h < 0) return {};
  unsigned pnOffset = unsigned(h) - PNLength;
  uint8_t payload[32] = {};
  if (Zquic::FrameCodec::writePing(payload, sizeof(payload)) != 1)
    return {};
  int n = Zquic::PktProt::protectShort(
    buf->data_(), buf->size, tx, pn,
    span_(buf->data_(), unsigned(h)), span_(payload, sizeof(payload)),
    pnOffset, PNLength);
  if (n < 0) return {};
  buf->skip = 0;
  buf->length = unsigned(n);
  return buf;
}

static ZmRef<ZiIOBuf> zeroRTTLongHdr(uint64_t pn)
{
  ZmRef<ZiIOBuf> buf = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return Zquic::pktRxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
  if (ZuUnlikely(!buf)) return {};
  Zquic::CxnID dcid{"server01"};
  Zquic::CxnID scid{"client01"};
  enum { PNLength = 2 };
  int h = Zquic::Pkt::writeLong(
    buf->data_(), buf->size, Zquic::PktType::ZeroRTT, dcid, scid, 8, PNLength);
  if (h < 0 ||
      Zquic::PktNumber::encode(
	buf->data_() + h, buf->size - unsigned(h), pn, PNLength) != PNLength)
    return {};
  buf->skip = 0;
  buf->length = unsigned(h + PNLength);
  return buf;
}
#endif

static bool parseFrame(const uint8_t *b, int n, Zquic::Frame &frame)
{
  unsigned used = 0;
  return n > 0 && !Zquic::FrameCodec::parse(
    ZuBSpan{b, unsigned(n)}, frame, used) &&
    used == unsigned(n);
}

static ZmRef<ZiIOBuf> streamPkt(
  uint64_t id, uint64_t offset, ZuBSpan payload, bool fin,
  Zquic::Frame &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return Zquic::pktRxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
  if (ZuUnlikely(!packet)) return nullptr;
  int n = Zquic::FrameCodec::writeStream(
    packet->data_(), packet->size, id, offset, payload, fin);
  if (n <= 0) return nullptr;
  packet->skip = 0;
  packet->length = unsigned(n);
  if (Zquic::FrameCodec::parse(
      ZuBSpan{packet->data(), packet->length}, frame, used) ||
      used != packet->length)
    return nullptr;
  return packet;
}

static bool consumeExact_(Zquic::RxStream &rx, unsigned n, ZuBSpan expected)
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

#ifdef Zquic_DEBUG
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

void testStreamIDs()
{
  ZuTestScope(testStreamIDs);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto c0 = client->stream(Zquic::StreamType::Duplex);
  auto c1 = client->stream(Zquic::StreamType::Simplex);
  ZuCHECK(c0->id() == 0 && c1->id() == 2, "client stream IDs mismatch");

  ZmRef<TestLink> server = testLink(&app, true);
  auto s0 = server->stream(Zquic::StreamType::Duplex);
  auto s1 = server->stream(Zquic::StreamType::Simplex);
  ZuCHECK(s0->id() == 1 && s1->id() == 3, "server stream IDs mismatch");
  ZuCHECK(server->findStream(1) == s0, "stream table lookup failed");
}

void testStreamFrameDelivery()
{
  ZuTestScope(testStreamFrameDelivery);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto stream = client->stream(Zquic::StreamType::Duplex);

  Zquic::Frame frame;
  unsigned used = 0;
  auto packet = streamPkt(stream->id(), 0, "hello", false, frame, used);
  ZuCHECK(packet, "STREAM frame setup failed");

  Zquic::BufDiag diag;
  ZuCHECK(stream->processFrame(frame, packet, &diag) == 0 &&
      stream->processed == 1 &&
      stream->rxBytes() == 5,
    "STREAM frame delivery mismatch");

  auto &rx = stream->rxStream();
  ZuCHECK(consumeExact_(rx, 5, "hello"),
    "STREAM payload was not queued for receive");
  ZuCHECK(rx.empty(), "STREAM receive queue consume failed");

  packet = streamPkt(stream->id(), 5, {}, true, frame, used);
  ZuCHECK(packet, "STREAM FIN frame setup failed");
  ZuCHECK(stream->processFrame(frame, packet, &diag) == 0 &&
      stream->finReceived() && stream->rxComplete() &&
      stream->finalSize() == 5,
    "STREAM FIN delivery mismatch");

  packet = streamPkt(stream->id(), 5, "!", true, frame, used);
  ZuCHECK(packet,
    "conflicting STREAM FIN setup failed");
  ZuCHECK(!stream->receiveFrame(frame, packet, &diag),
    "conflicting final-size STREAM was accepted");

  packet = streamPkt(stream->id() + 4, 0, "x", false, frame, used);
  ZuCHECK(packet,
    "wrong-ID STREAM setup failed");
  ZuCHECK(!stream->receiveFrame(frame, packet, &diag),
    "wrong-ID STREAM was accepted");
}

void testStreamRxSliceDelivery()
{
  ZuTestScope(testStreamRxSliceDelivery);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto stream = client->stream(Zquic::StreamType::Duplex);

  ZmRef<ZiIOBuf> packet = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return Zquic::pktRxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
  ZuCHECK(bool(packet), "packet buffer allocation failed");
  if (!packet) return;
  int n = Zquic::FrameCodec::writeStream(
    packet->data_(), packet->size, stream->id(), 0, "slice", false);
  ZuCHECK(n > 0, "packet-backed STREAM frame write failed");
  packet->skip = 0;
  packet->length = unsigned(n);

  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuBSpan{packet->data(), packet->length}, frame, used) &&
      used == packet->length,
    "packet-backed STREAM frame parse failed");

  Zquic::BufDiag diag;
  ZuCHECK(stream->processFrame(frame, packet, &diag) == 0 &&
      stream->processed == 1 &&
      stream->rxBytes() == 5,
    "packet-backed STREAM slice mismatch");
  packet = nullptr;

  auto &rx = stream->rxStream();
  ZuCHECK(consumeExact_(rx, 5, "slice"),
    "packet-backed STREAM slice payload mismatch");
  ZuCHECK(rx.empty(), "packet-backed STREAM slice consume failed");
}

void testOutOfOrderStreamDelivery()
{
  ZuTestScope(testOutOfOrderStreamDelivery);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto stream = client->stream(Zquic::StreamType::Duplex);

  Zquic::Frame frame;
  unsigned used = 0;
  Zquic::BufDiag diag;

  auto packet = streamPkt(stream->id(), 5, "world", false, frame, used);
  ZuCHECK(packet,
    "out-of-order STREAM setup failed");
  ZuCHECK(stream->processFrame(frame, packet, &diag) == 0 &&
      stream->processed == 1 &&
      !stream->rxBytes() &&
      stream->rxPending() == 1 &&
      !stream->rxQueued(),
    "out-of-order STREAM pending state mismatch");
  ZuCHECK(stream->processFrame(frame, packet, &diag) == 0 &&
      stream->rxPending() == 1,
    "duplicate pending STREAM copied or queued again");

  packet = streamPkt(stream->id(), 0, "hello", false, frame, used);
  ZuCHECK(packet,
    "gap-filling STREAM setup failed");
  ZuCHECK(stream->processFrame(frame, packet, &diag) == 0 &&
      stream->rxBytes() == 10 &&
      !stream->rxPending() &&
      stream->rxQueued() == 2,
    "gap-filling STREAM did not drain pending data");

  auto &rx = stream->rxStream();
  ZuCHECK(consumeExact_(rx, 5, "hello"),
    "first reordered STREAM payload mismatch");
  ZuCHECK(consumeExact_(rx, 5, "world"),
    "second reordered STREAM payload mismatch");
  ZuCHECK(rx.empty(), "second reordered STREAM consume failed");

  auto split = client->stream(Zquic::StreamType::Duplex);
  diag = {};
  packet = streamPkt(split->id(), 5, "world", false, frame, used);
  ZuCHECK(packet,
    "split pending STREAM setup failed");
  ZuCHECK(split->processFrame(frame, packet, &diag) == 0 &&
      split->rxPending() == 1 &&
      !split->rxQueued(),
    "split pending STREAM state mismatch");

  packet = streamPkt(split->id(), 0, "helloworldtails", false, frame, used);
  ZuCHECK(packet,
    "interior-overlap STREAM setup failed");
  ZuCHECK(split->processFrame(frame, packet, &diag) == 0 &&
      split->rxBytes() == 15 &&
      !split->rxPending() &&
      split->rxQueued() == 3,
    "interior-overlap STREAM did not copy only novel spans");

  auto &splitRx = split->rxStream();
  ZuCHECK(consumeExact_(splitRx, 5, "hello"),
    "split first STREAM payload mismatch");
  ZuCHECK(consumeExact_(splitRx, 5, "world"),
    "split second STREAM payload mismatch");
  ZuCHECK(consumeExact_(splitRx, 5, "tails"),
    "split third STREAM payload mismatch");
  ZuCHECK(splitRx.empty(), "split STREAM consume failed");
}

void testRxRetirement()
{
  ZuTestScope(testRxRetirement);

  App app;
  Zquic::Frame frame;
  unsigned used = 0;

  ZmRef<TestLink> stalled = testLink(&app, true);
  stalled->setLocalStreamLimit(Zquic::StreamType::Duplex, 1);
  auto packet = streamPkt(0, 0, "abc", false, frame, used);
  ZuCHECK(packet && stalled->receiveFrame(frame, packet) == 0,
    "stalled receive retirement setup failed");
  auto stream = stalled->findStream(0);
  ZuCHECK(stream && stream->rxStream().length() == 3 &&
      stream->rxRetirable() == 3 && !stream->rxRetired() &&
      stalled->rxDataCreditUsed() == 3 && !stalled->rxDataRetired(),
    "native delivery returned QUIC receive credit prematurely");

  stream->rxStream().advance(3);
  ZuCHECK(!stream->rxStream() && stream->rxRetirable() == 3 &&
      !stream->rxRetired() && !stalled->rxDataRetired(),
    "native queue removal returned QUIC receive credit implicitly");
  ZuCHECK(stream->retireRx(2) && stream->rxRetired() == 2 &&
      stalled->rxDataRetired() == 2 &&
      !stream->retireRx(2) && stream->retireRx(1) &&
      stream->rxRetired() == 3 && stalled->rxDataRetired() == 3,
    "explicit QUIC receive retirement was not exact");

  {
    App small{Zquic::MinUDPPayload, 8};
    auto credit = testLink(&small, true);
    credit->setLocalStreamLimit(Zquic::StreamType::Duplex, 1);
    packet = streamPkt(0, 0, "abcdef", false, frame, used);
    ZuCHECK(packet && credit->receiveFrame(frame, packet) == 0,
      "small-window receive retirement setup failed");
    auto receiver = credit->findStream(0);
    unsigned queued = credit->txFlushQueued;
    ZuCHECK(receiver && receiver->retireRx(6) &&
	credit->rxDataCreditLimit() == 14 && receiver->rxCreditLimit() == 14 &&
	credit->txFlushQueued == queued + 2,
      "retired bytes schedule connection and stream credit flushes without application writes");
  }

  ZmRef<TestLink> reset = testLink(&app, true);
  reset->setLocalStreamLimit(Zquic::StreamType::Duplex, 1);
  packet = streamPkt(0, 3, "de", false, frame, used);
  ZuCHECK(packet && reset->receiveFrame(frame, packet) == 0,
    "out-of-order receive retirement setup failed");
  stream = reset->findStream(0);
  ZuCHECK(stream && stream->rxAdmitted() == 2 && !stream->rxBytes() &&
      !stream->rxRetirable() && !stream->retireRx(1) &&
      reset->rxDataCreditUsed() == 2 && !reset->rxDataRetired(),
    "out-of-order QUIC bytes became retirable across a gap");

  uint8_t b[32];
  int n = Zquic::FrameCodec::writeResetStream(
    b, sizeof(b), 0, 7, 5);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used) &&
      reset->receiveFrame(frame) == 0 &&
      stream->resetReceived() && stream->finalSize() == 5 &&
      stream->rxRetired() == 5 && reset->rxDataCreditUsed() == 5 &&
      reset->rxDataRetired() == 5,
    "RESET_STREAM did not admit and retire its exact final-size gap");
}

void testStreamTxRetention()
{
  ZuTestScope(testStreamTxRetention);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto stream = client->stream(Zquic::StreamType::Duplex);

  {
    auto tx = stream->txStream_();
    tx << "abc" << Zi::flush();
    tx << "def" << Zi::flush();
  }
  stream->fin();

  ZuCHECK(stream->txBytes() == 6 &&
      stream->txBufferedBytes() == 6 &&
      stream->txRangeCount() == 2 &&
      stream->finSent() &&
      !stream->finReady() &&
      !stream->finDequeued(),
    "stream Tx retention counters mismatch");

  uint64_t finOffset = ~uint64_t{0};
  ZuCHECK(!stream->dequeueFin(finOffset),
    "FIN dequeued before retained data drained");

  Zquic::TxRange range;
  ZuCHECK(stream->txRange(0, range) &&
      range.buf && !range.offset && range.length == 3 &&
      !range.streamOffset &&
      !memcmp(range.buf->data_() + range.offset, "abc", 3),
    "first retained Tx range mismatch");
  ZuCHECK(stream->dequeueTxRange(range) &&
      range.length == 3 &&
      !range.streamOffset &&
      !memcmp(range.buf->data_() + range.offset, "abc", 3) &&
      stream->txBufferedBytes() == 3 &&
      stream->txRangeCount() == 1,
    "first Tx range dequeue mismatch");
  ZuCHECK(stream->dequeueTxRange(range) &&
      range.length == 3 &&
      range.streamOffset == 3 &&
      !memcmp(range.buf->data_() + range.offset, "def", 3) &&
      !stream->txBufferedBytes() &&
      !stream->txRangeCount() &&
      stream->finReady(),
    "second Tx range dequeue mismatch");
  ZuCHECK(stream->dequeueFin(finOffset) &&
      finOffset == 6 &&
      stream->finDequeued() &&
      !stream->finReady(),
    "FIN dequeue state mismatch");
  ZuCHECK(!stream->dequeueFin(finOffset),
    "FIN dequeued more than once");
  ZuCHECK(!stream->dequeueTxRange(range),
    "empty Tx range dequeue succeeded");
}

void testStreamTxRetainedBuffers()
{
  ZuTestScope(testStreamTxRetainedBuffers);

  App app;
  auto link = testLink(&app);
  auto stream = link->stream(Zquic::StreamType::Duplex);
  auto tx = stream->txStream_();
  auto backing = tx.allocBuf(7);
  backing->append(reinterpret_cast<const uint8_t *>("A-gap-BC"), 8);
  auto original = backing->data();
  ZmRef<ZiIOBuf> first = new Zquic::StreamRxPQueue::Node{
    backing, original, 1, app.streamRxAllocator(), nullptr, 0};
  ZmRef<ZiIOBuf> second = new Zquic::StreamRxPQueue::Node{
    backing, original + 6, 2, app.streamRxAllocator(), nullptr, 0};
  auto firstView = first.ptr(), secondView = second.ptr();
  ZuCHECK(tx.appendBuf(ZuMv(first)) && tx.appendBuf(ZuMv(second)) &&
      !first && !second,
    "QUIC narrowed buffer ownership transfer failed");
  tx << "D" << Zi::Flush{};
  ZuCHECK(stream->txBytes() == 4 && stream->txRangeCount() == 3,
    "QUIC owned buffer and ordinary node stream offsets mismatch");
  Zquic::TxRange selected;
  Zquic::TxRange consumed;
  ZuCHECK(stream->txRange(0, selected) && selected.buf.ptr() == firstView &&
      selected.buf->data_() + selected.offset == original && selected.length == 1 &&
      stream->commitTxRange(consumed, selected, 1),
    "QUIC first narrowed buffer lost original backing");
  ZuCHECK(stream->txRange(0, selected) && selected.buf.ptr() == secondView &&
      selected.buf->data_() + selected.offset == original + 6 && selected.streamOffset == 1 &&
      stream->commitTxRange(consumed, selected, 1) &&
      stream->txRange(0, selected) && selected.buf->data() == original + 7 &&
      selected.length == 1 && selected.streamOffset == 2,
    "QUIC partial buffer consumption copied or lost positions");
  ZuCHECK(backing->length == 8 && backing->skip == 7 &&
      !memcmp(backing->data(), "A-gap-BC", 8),
    "QUIC narrowed descriptors must retain original plaintext bytes");
  stream->reset(0);
  ZuCHECK(!stream->txRangeCount() && !stream->txQueueCount(),
    "QUIC reset failed to release mixed range metadata and plaintext nodes");
}

void testStreamTxQueueLimit()
{
  ZuTestScope(testStreamTxQueueLimit);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zquic::StreamType::Duplex);
  stream->txQueueMax(1);
  unsigned errors = 0;
  stream->txErrorFn(ZiTxErrorFn{
    [&errors](ZeException &) {
      ++errors;
      return true;
    }});

  auto tx1 = stream->txStream_();
  tx1 << "one" << Zi::flush();
  ZuCHECK(!tx1.failed() && stream->txQueueCount() == 1,
    "first QUIC stream Tx buffer was not admitted");

  auto tx2 = stream->txStream_();
  tx2 << "two" << Zi::flush();
  ZuCHECK(tx2.failed() && errors == 1 && stream->txQueueCount() == 1,
    "QUIC stream Tx queue overflow was not reported");

  Zquic::TxRange range;
  ZuCHECK(stream->dequeueTxRange(range) && !stream->txQueueCount(),
    "QUIC stream Tx admission was not released on dequeue");

  auto tx3 = stream->txStream_();
  tx3 << "three" << Zi::flush();
  ZuCHECK(!tx3.failed() && stream->txQueueCount() == 1 && errors == 1,
    "QUIC stream Tx queue did not reopen after dequeue");
}

void testStreamTxUnackd()
{
  ZuTestScope(testStreamTxUnackd);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zquic::StreamType::Duplex);
  ZuCHECK(stream &&
      stream->recordTxUnackd(10, 10, false) &&
      stream->txUnackdCount() == 1 &&
      stream->txUnackdBytes() == 10,
    "stream unackd range setup failed");

  ZuCHECK(stream->ackTxUnackd(13, 4) &&
      stream->txStillUnackd(10, 3, false) &&
      !stream->txStillUnackd(13, 4, false) &&
      stream->txStillUnackd(17, 3, false) &&
      stream->txUnackdCount() == 2 &&
      stream->txUnackdBytes() == 6,
    "stream unackd partial ACK did not split state");

  ZuCHECK(stream->ackTxUnackd(10, 3, false) &&
      !stream->txStillUnackd(10, 3, false) &&
      stream->txStillUnackd(17, 3, false) &&
      stream->txUnackdCount() == 1 &&
      stream->txUnackdBytes() == 3,
    "stream unackd exact ACK did not remove range");
}

void testStreamTxUnackdFin()
{
  ZuTestScope(testStreamTxUnackdFin);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zquic::StreamType::Duplex);
  ZuCHECK(stream &&
      stream->recordTxUnackd(20, 5, true) &&
      stream->txUnackdCount() == 1 &&
      stream->txUnackdBytes() == 6,
    "stream unackd data+FIN setup failed");

  ZuCHECK(stream->ackTxUnackd(20, 5, false) &&
      !stream->txStillUnackd(20, 5, false) &&
      stream->txStillUnackd(25, 0, true) &&
      stream->txUnackdCount() == 1 &&
      stream->txUnackdBytes() == 1,
    "stream unackd data ACK did not retain FIN");

  ZuCHECK(stream->ackTxUnackd(25, 0, true) &&
      !stream->txStillUnackd(25, 0, true) &&
      !stream->txUnackdCount() &&
      !stream->txUnackdBytes(),
    "stream unackd FIN ACK did not clear sentinel");
}

void testLinkStreamTxUnackdAck()
{
  ZuTestScope(testLinkStreamTxUnackdAck);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zquic::StreamType::Duplex);

  link->sendStreamRef(7, stream, 40, 5);
  ZuCHECK(stream->txUnackdCount() == 1 &&
      stream->txUnackdBytes() == 5 &&
      stream->txStillUnackd(40, 5),
    "sent STREAM ref did not populate unackd state");

  link->ackThrough(7);
  ZuCHECK(!stream->txUnackdCount() &&
      !stream->txUnackdBytes() &&
      !stream->txStillUnackd(40, 5),
    "ACKd STREAM ref did not clear unackd state");
  link->teardown();
}

void streamRetxUnackdIdempotent()
{
  ZuTestScope(streamRetxUnackdIdempotent);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zquic::StreamType::Duplex);

  link->sendStreamRef(0, stream, 40, 5);
  ZuCHECK(stream->txUnackdCount() == 1 &&
      stream->txUnackdBytes() == 5 &&
      stream->txStillUnackd(40, 5),
    "sent STREAM ref did not populate unackd state");

  link->sendAckEliciting(4);
  link->ackOnly(4);

  Zquic::PktNumSpace::T level = Zquic::PktNumSpace::Initial;
  Zquic::SentFrameRef ref;
  ZuCHECK(link->nextRetransmitRef(level, ref) &&
      level == Zquic::PktNumSpace::AppData &&
      ref.kind == Zquic::SentFrameKind::Stream &&
      ref.streamID == uint64_t(stream->id()) &&
      ref.offset == 40 &&
      ref.length == 5 &&
      !ref.fin,
    "lost STREAM ref was not queued for retransmit");

  link->sendStreamRef(5, stream, ref.offset, ref.length, ref.fin);
  ZuCHECK(stream->txUnackdCount() == 1 &&
      stream->txUnackdBytes() == 5 &&
      stream->txStillUnackd(40, 5),
    "retransmitted STREAM ref duplicated unackd owner state");

  link->sendAckEliciting(9);
  link->ackOnly(9);
  ZuCHECK(stream->txUnackdCount() == 1 &&
      stream->txUnackdBytes() == 5 &&
      stream->txStillUnackd(40, 5),
    "lost retransmitted STREAM ref changed owner state");

  link->ackOnly(5);
  ZuCHECK(!stream->txUnackdCount() &&
      !stream->txUnackdBytes() &&
      !stream->txStillUnackd(40, 5),
    "late ACK of retained lost retransmit did not clear owner state");

  link->ackOnly(0);
  ZuCHECK(!stream->txUnackdCount() &&
      !stream->txUnackdBytes(),
    "late ACK of retained lost STREAM ref changed owner state");
  link->teardown();
}

void testStreamRetxPacketBudget()
{
  ZuTestScope(testStreamRetxPacketBudget);

#ifdef Zquic_DEBUG
  App app{Zquic::BufSize};
  auto link = testLink(&app, true);
  link->initServerPath();
  link->validatePath();
  link->growActivePath(1472);
  Zquic::TrafficSecret secret;
  ZuCHECK(trafficSecret(secret, 0x81) &&
      link->installOneRTT(secret, secret, {}),
    "retransmit packet budget secret setup failed");
  constexpr unsigned Size = 1300;
  uint8_t expected[Size], decoded[Size]{};
  unsigned coverage[Size]{};
  for (unsigned i = 0; i < Size; ++i) expected[i] = uint8_t(i * 13 + 7);
  auto stream = link->stream(Zquic::StreamType::Duplex);
  stream->txCredit(Size);
  link->grantDataCredit(Size);
  {
    auto tx = stream->txStream_();
    tx << span_(expected, Size) << Zi::flush();
  }
  stream->fin();
  Zquic::SentFrameRef original;
  unsigned n = link->flushRecordedRefs(&original, 1);
  bool ready = n == 1 && original.kind == Zquic::SentFrameKind::Stream &&
    original.range.buf && original.length == Size && original.fin;
  ZuCHECK(ready, "old MTU packet did not retain the complete STREAM plus FIN");
  if (!ready) { link->teardown(); return; }
  auto backing = original.range.buf;
  link->sendAckEliciting(4);
  link->ackOnly(4);
  Zquic::PktNumSpace::T lostLevel;
  Zquic::SentFrameRef lost;
  bool reclaimed = link->nextRetransmitRef(lostLevel, lost) &&
    lostLevel == Zquic::PktNumSpace::AppData && lost.range.buf == backing &&
    lost.offset == original.offset && lost.length == original.length && lost.fin;
  ZuCHECK(reclaimed, "old packet loss did not yield its actual retained recovery reference");
  if (!reclaimed) { link->teardown(); return; }
  original = ZuMv(lost);
  uint64_t credit = stream->txCreditAvailable();
  link->initServerPath(); // A promoted path starts at the 1200-byte minimum.
  link->validatePath();
  link->sparseAck();
  Zquic::SentFrameRef head = original;
  ZmRef<ZiIOBuf> wire;
  bool withAck = false;
  bool sent = link->protectedStreamRetx(head, wire, true, withAck);
  bool split = !sent && wire && wire->length <= 1200 && withAck &&
    head.length && head.length < original.length && !head.fin &&
    head.range.buf == backing && link->ackPending();
  ZuCHECK(split,
    "failed first retransmission must fit the new MTU with sparse ACKs, keep "
    "the original backing, and retain the pending ACK");
  if (!split) { link->teardown(); return; }
  ZuCHECK(link->requeueStream(head),
    "failed retransmission head was not retained with its queued tail");

  unsigned packets = 0, fins = 0, total = 0;
  bool valid = true;
  Zquic::PktNumSpace::T level;
  Zquic::SentFrameRef ref;
  while (packets < 8 && link->nextRetransmitRef(level, ref)) {
    wire = nullptr;
    bool ok = level == Zquic::PktNumSpace::AppData &&
      ref.kind == Zquic::SentFrameKind::Stream &&
      link->protectedStreamRetx(ref, wire, false, withAck);
    valid &= ok && wire && wire->length <= 1200 && ref.range.buf == backing &&
      ref.offset <= Size && ref.length <= Size - ref.offset &&
      (!ref.fin || ref.offset + ref.length == Size);
    if (!ok || ref.offset > Size || ref.length > Size - ref.offset) break;
    memcpy(decoded + ref.offset, backing->data_() + ref.range.offset,
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
      !link->ackPending() && stream->txCreditAvailable() == credit &&
      stream->txUnackdBytes() == Size + 1,
    "MTU/ACK repacketization must retain every byte exactly once, preserve "
    "flow credit, and carry FIN only at the original tail");
  ZuCHECK(stream->ackTxUnackd(0, Size, true) && !stream->txUnackdBytes(),
    "repacketized STREAM and FIN did not retire after ACK");
  link->teardown();
#endif
}

void protectedPathResponseSize()
{
  ZuTestScope(protectedPathResponseSize);

  App app;
  Zquic::TrafficSecret secret;
  ZuCHECK(trafficSecret(secret, 0x79), "path response secret failed");
  for (unsigned received : {0U, 400U, 32U}) {
    auto link = testLink(&app, true);
    link->initServerPath();
    if (!received) link->validatePath();
    else link->pathReceived(received);
    unsigned wireBytes = 0;
    ZuCHECK(link->installOneRTT(secret, secret, {}) &&
	link->protectedPathResponse(wireBytes),
      "protected PATH_RESPONSE could not be sent within its path budget");
    ZuCHECK(received == 32 ? wireBytes > 0 && wireBytes < 1200 &&
	link->pathAntiAmp() == received * 3 - wireBytes : wireBytes == 1200,
      "protected PATH_RESPONSE must reach 1200 bytes unless anti-amplification "
      "limits the response");
    ZuCHECK(received != 400 || !link->pathAntiAmp(),
      "padded response did not consume its exact anti-amplification budget");
    link->teardown();
  }
}

void testStreamProducerCredit()
{
  ZuTestScope(testStreamProducerCredit);

  App app;
  app.deferredTx = true;
  auto link = testLink(&app);
  auto stream = link->stream(Zquic::StreamType::Duplex);
  unsigned callbacks = 0;
  bool stable = true;
  ZmRef<ZiIOBuf> next = app.streamTxAllocator()->allocBuf();
  next->append(reinterpret_cast<const uint8_t *>("def"), 3);
  Zquic::StreamFrameInfo retained;
  auto packetize = [&stream](unsigned credit, Zquic::StreamFrameInfo &info) {
    uint8_t out[64];
    Zquic::PktBudget budget;
    budget.flow = credit;
    Zquic::PktAssembly assembly;
    return Zquic::StreamPktizer::writeNext(
      out, sizeof(out), budget, assembly, *stream, &info);
  };
  app.onTx([&app, &stream, &callbacks, &stable, &next, &retained, &packetize]() {
    stream->txCredit(6);
    stream->dequeued = [&app, &stream, &callbacks, &stable, &next]() {
      ++callbacks;
      stable &= app.mx()->invoked(app.txThread()) &&
	!stream->txBufferedBytes() && !stream->txQueueCount() &&
	stream->txCreditAvailable() == (callbacks == 1 ? 3 : 0) &&
	stream->txUnackdBytes() == (callbacks == 1 ? 3 : 7) &&
	stream->finDequeued() == (callbacks == 2);
      if (callbacks == 1) {
	auto tx = stream->txStream_();
	stable &= tx.appendBuf(ZuMv(next));
	stream->fin();
      }
    };
    auto tx = stream->txStream_();
    tx << "abc" << Zi::flush();
    stable &= packetize(0, retained) < 0 && !callbacks &&
      stream->txBufferedBytes() == 3;
    stable &= packetize(2, retained) > 0 && retained.length == 2 &&
      !callbacks && stream->txBufferedBytes() == 1;
    stable &= stream->recordTxUnackd(retained.offset, retained.length, false);
    stable &= packetize(1, retained) > 0 && retained.length == 1 &&
      !callbacks && !stream->txBufferedBytes();
    stable &= stream->recordTxUnackd(retained.offset, retained.length, false);
  });
  app.onTx([&stream, &callbacks, &stable, &retained, &packetize]() {
    stable &= callbacks == 1 && stream->txBufferedBytes() == 3 &&
      stream->txUnackdBytes() == 3;
    stable &= packetize(3, retained) > 0 && retained.fin &&
      retained.length == 3 && callbacks == 1;
    stable &= stream->recordTxUnackd(retained.offset, retained.length, true);
  });
  app.onTx([&stream, &callbacks, &stable, &retained, &packetize]() {
    stable &= callbacks == 2 && retained.range.buf &&
      !memcmp(retained.range.buf->data_() + retained.range.offset, "def", 3);
    stable &= stream->ackTxUnackd(0, 6, true) &&
      !stream->ackTxUnackd(0, 6, true) && callbacks == 2;
    Zquic::StreamFrameInfo empty;
    stable &= packetize(3, empty) == 0;
    stream->dequeued = {};
  });
  link->teardown();
  ZuCHECK(stable && callbacks == 2,
    "producer credit must follow full packetization on AppTx, retain recovery "
    "bytes, allow bounded refill, and leave ACK/FIN idempotent");

  auto canceled = testLink(&app);
  auto pending = canceled->stream(Zquic::StreamType::Duplex);
  app.onTx([&pending, &callbacks, &stable]() {
    pending->txCredit(3);
    pending->dequeued = [&callbacks]() { ++callbacks; };
    auto tx = pending->txStream_();
    tx << "abc" << Zi::flush();
    Zquic::TxRange range;
    stable &= pending->dequeueTxRange(range);
    pending->reset(1);
  });
  app.onTx([&pending]() {
    pending->dequeued = {};
  });
  canceled->teardown();
  ZuCHECK(stable && callbacks == 2,
    "a canceled stream must suppress already queued producer credit");
}

void testStreamPktizer()
{
  ZuTestScope(testStreamPktizer);

  uint8_t prefix[32];
  uint8_t assembled[64];
  ZuBSpan prefixPayload{"prefix-payload"};
  int prefixLen = Zquic::FrameCodec::writeStreamPrefix(
    prefix, sizeof(prefix), 5, 9, prefixPayload.length(), true);
  ZuCHECK(prefixLen > 0 &&
      unsigned(prefixLen) + prefixPayload.length() <= sizeof(assembled),
    "STREAM prefix writer failed");
  memcpy(assembled, prefix, unsigned(prefixLen));
  memcpy(assembled + prefixLen, prefixPayload.data(), prefixPayload.length());
  Zquic::Frame prefixFrame;
  unsigned prefixUsed = 0;
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuBSpan{assembled, unsigned(prefixLen) + prefixPayload.length()},
      prefixFrame, prefixUsed) &&
      prefixUsed == unsigned(prefixLen) + prefixPayload.length() &&
      prefixFrame.type == Zquic::FrameType::Stream &&
      prefixFrame.streamID == 5 &&
      prefixFrame.offset == 9 &&
      prefixFrame.length == prefixPayload.length() &&
      prefixFrame.payload == prefixPayload &&
      prefixFrame.fin,
    "STREAM prefix-only assembly parse mismatch");

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto stream = client->stream(Zquic::StreamType::Duplex);
  stream->txCredit(6);
  {
    auto tx = stream->txStream_();
    tx << "abc" << Zi::flush();
    tx << "def" << Zi::flush();
  }
  stream->fin();

  uint8_t b[128];
  Zquic::PktBudget budget;
  Zquic::PktAssembly assembly;
  Zquic::StreamFrameInfo info;
  int n = Zquic::StreamPktizer::writeNext(
    b, sizeof(b), budget, assembly, *stream, &info);
  ZuCHECK(n > 0 &&
      assembly.streamAdded() &&
      budget.used == info.bytes &&
      info.streamID == 0 &&
      info.offset == 0 &&
      info.length == 3 &&
      info.bytes == unsigned(n) + info.length &&
      info.range.length == 3 &&
      !memcmp(info.range.buf->data_() + info.range.offset, "abc", 3) &&
	  !info.fin &&
	  stream->txRangeCount() == 1,
	"first packetized STREAM accounting mismatch");
  unsigned usedAfterFirst = budget.used;

  Zquic::Frame frame;
  unsigned used = 0;
  memcpy(assembled, b, unsigned(n));
  memcpy(assembled + n, info.range.buf->data_() + info.range.offset,
    info.range.length);
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuBSpan{assembled, info.bytes},
      frame, used) &&
      used == info.bytes &&
      frame.type == Zquic::FrameType::Stream &&
      frame.streamID == 0 &&
      frame.offset == 0 &&
      frame.length == 3 &&
      frame.payload == "abc" &&
      !frame.fin,
    "first packetized STREAM parse mismatch");
  n = Zquic::StreamPktizer::writeNext(
    b, sizeof(b), budget, assembly, *stream, &info);
  ZuCHECK(n > 0 &&
      budget.used == usedAfterFirst + info.bytes &&
      info.offset == 3 &&
      info.length == 3 &&
	      info.bytes == unsigned(n) + info.length &&
	      info.range.length == 3 &&
	      !memcmp(info.range.buf->data_() + info.range.offset, "def", 3) &&
	      info.fin &&
	      !stream->txRangeCount() &&
	      stream->finDequeued() &&
	      !stream->finReady(),
	    "second packetized STREAM accounting mismatch");
  memcpy(assembled, b, unsigned(n));
  memcpy(assembled + n, info.range.buf->data_() + info.range.offset,
    info.range.length);
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuBSpan{assembled, info.bytes},
      frame, used) &&
      used == info.bytes &&
	      frame.offset == 3 &&
	      frame.length == 3 &&
	      frame.payload == "def" &&
      frame.fin,
    "second packetized STREAM parse mismatch");

  budget = {};
	  assembly = {};
	  ZuCHECK(!Zquic::StreamPktizer::writeNext(
	      b, sizeof(b), budget, assembly, *stream, &info),
	    "packetizer wrote a frame for an empty stream");

  auto blocked = client->stream(Zquic::StreamType::Duplex);
  blocked->txCredit(7);
  {
    auto tx = blocked->txStream_();
    tx << "blocked" << Zi::flush();
  }
  budget = {};
  budget.pmtu = budget.congestion = budget.antiAmplification = 2;
  assembly = {};
  ZuCHECK(Zquic::StreamPktizer::writeNext(
      b, sizeof(b), budget, assembly, *blocked, &info) < 0 &&
      blocked->txRangeCount() == 1 &&
      blocked->txBufferedBytes() == 7 &&
      !assembly.streamAdded(),
    "packetizer consumed data without packet budget");

  auto split = client->stream(Zquic::StreamType::Duplex);
  split->txCredit(10);
  {
    auto tx = split->txStream_();
    tx << "abcdefghij" << Zi::flush();
  }
  budget = {};
  budget.pmtu = budget.congestion = budget.antiAmplification = 8;
  assembly = {};
  n = Zquic::StreamPktizer::writeNext(
    b, sizeof(b), budget, assembly, *split, &info);
  ZuCHECK(n > 0 &&
      unsigned(n) < info.bytes &&
      info.streamID == uint64_t(split->id()) &&
      info.offset == 0 &&
      info.length == 5 &&
      info.bytes == 8 &&
      info.range.length == 5 &&
      !memcmp(info.range.buf->data_() + info.range.offset, "abcde", 5) &&
      split->txRangeCount() == 1 &&
      split->txBufferedBytes() == 5,
    "packetizer did not split oversized stream range");
  memcpy(assembled, b, unsigned(n));
  memcpy(assembled + n, info.range.buf->data_() + info.range.offset,
    info.range.length);
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuBSpan{assembled, info.bytes},
      frame, used) &&
      used == info.bytes &&
      frame.streamID == uint64_t(split->id()) &&
      frame.offset == 0 &&
      frame.length == 5 &&
      frame.payload == "abcde",
    "split packetized STREAM parse mismatch");
  Zquic::TxRange tail;
  ZuCHECK(split->txRange(0, tail) &&
      tail.length == 5 &&
      tail.streamOffset == 5 &&
      !memcmp(tail.buf->data_() + tail.offset, "fghij", 5),
	    "split packetizer retained tail mismatch");
}

void testCtlSendFailRetainsFrame()
{
  ZuTestScope(testCtlSendFailRetainsFrame);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  ZuCHECK(client->queuePathResponse() &&
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
  ZuCHECK(
      controls->queuePathResponse(1) &&
      controls->queuePathResponse(2) &&
      controls->queuePathResponse(3),
    "multi-control setup failed");
  Zquic::SentFrameKind::T kinds[8]{};
  uint64_t streamIDs[8]{};
  unsigned refs = controls->flushFrameRefs(kinds, streamIDs, 8);
  ZuCHECK(refs == 3 &&
      kinds[0] == Zquic::SentFrameKind::Control &&
      kinds[1] == Zquic::SentFrameKind::Control &&
      kinds[2] == Zquic::SentFrameKind::Control &&
      !controls->queuedControlFrames(),
    "runtime did not assemble multiple control frames into one packet");

  ZmRef<TestLink> streams = testLink(&app);
  auto s0 = streams->stream(Zquic::StreamType::Duplex);
  auto s1 = streams->stream(Zquic::StreamType::Duplex);
  streams->grantDataCredit(20000);
  s0->txCredit(20000);
  s1->txCredit(20000);
  {
    auto tx = s0->txStream_();
    tx << "alpha" << Zi::flush();
  }
  {
    auto tx = s1->txStream_();
    tx << "bravo" << Zi::flush();
  }
  if (!streams->scheduledStreams()) {
    streams->scheduleStream(s0);
    streams->scheduleStream(s1);
  }
  refs = streams->flushFrameRefs(kinds, streamIDs, 8);
  ZuCHECK(refs == 2 &&
      kinds[0] == Zquic::SentFrameKind::Stream &&
      kinds[1] == Zquic::SentFrameKind::Stream &&
      streamIDs[0] == uint64_t(s0->id()) &&
      streamIDs[1] == uint64_t(s1->id()),
    "runtime did not assemble multiple stream frames into one packet");

  ZmRef<TestLink> finOnly = testLink(&app);
  auto finStream = finOnly->stream(Zquic::StreamType::Duplex);
  finOnly->grantDataCredit(20000);
  finStream->txCredit(20000);
  {
    auto tx = finStream->txStream_();
    tx << "payload" << Zi::flush();
  }
  Zquic::SentFrameRef sentRefs[8];
  refs = finOnly->flushSentRefs(sentRefs, 8);
  ZuCHECK(refs == 1 && sentRefs[0].kind == Zquic::SentFrameKind::Stream &&
      sentRefs[0].length == 7 && !sentRefs[0].fin,
    "runtime did not send first stream data frame");
  finStream->fin();
  refs = finOnly->flushSentRefs(sentRefs, 8);
  ZuCHECK(refs == 1 && sentRefs[0].kind == Zquic::SentFrameKind::Stream &&
      sentRefs[0].streamID == uint64_t(finStream->id()) &&
      sentRefs[0].offset == 7 && !sentRefs[0].length &&
      sentRefs[0].fin,
    "runtime lost FIN-only stream offset");
}

void streamRetxClipsUnackd()
{
  ZuTestScope(streamRetxClipsUnackd);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zquic::StreamType::Duplex);
  link->sendStreamRef(0, stream, 0, 10);
  link->sendStreamRef(1, stream, 2, 5);
  link->sendAckEliciting(4);
  link->ackOnly(1);
  link->ackOnly(4);

  Zquic::PktNumSpace::T level = Zquic::PktNumSpace::Initial;
  Zquic::SentFrameRef ref;
  ZuCHECK(link->nextRetransmitRef(level, ref) &&
      level == Zquic::PktNumSpace::AppData &&
      ref.kind == Zquic::SentFrameKind::Stream &&
      ref.offset == 0 &&
      ref.length == 2 &&
      !ref.fin,
    "stream retransmit did not clip before ACKd hole");
  ZuCHECK(link->nextRetransmitRef(level, ref) &&
      level == Zquic::PktNumSpace::AppData &&
      ref.kind == Zquic::SentFrameKind::Stream &&
      ref.offset == 7 &&
      ref.length == 3 &&
      !ref.fin,
    "stream retransmit did not requeue after ACKd hole");
  ZuCHECK(!link->nextRetransmitRef(level, ref),
    "stream retransmit clipping left extra work");
  link->teardown();
}

void testStreamRetxClipsUnackdFin()
{
  ZuTestScope(testStreamRetxClipsUnackdFin);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zquic::StreamType::Duplex);
  link->sendStreamRef(0, stream, 0, 10, true);
  ZuCHECK(stream->ackTxUnackd(0, 10, false) &&
      stream->txStillUnackd(10, 0, true),
    "stream unackd data ACK did not retain FIN before retransmit");

  link->sendAckEliciting(4);
  link->ackOnly(4);

  Zquic::PktNumSpace::T level = Zquic::PktNumSpace::Initial;
  Zquic::SentFrameRef ref;
  ZuCHECK(link->nextRetransmitRef(level, ref) &&
      level == Zquic::PktNumSpace::AppData &&
      ref.kind == Zquic::SentFrameKind::Stream &&
      ref.streamID == uint64_t(stream->id()) &&
      ref.offset == 10 &&
      !ref.length &&
      ref.fin,
    "stream retransmit did not clip ACKd data before retained FIN");
  ZuCHECK(!link->nextRetransmitRef(level, ref),
    "stream FIN clipping left extra retransmit work");
  link->teardown();
}

void cryptoRetxClipsUnackd()
{
  ZuTestScope(cryptoRetxClipsUnackd);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  Zquic::TrafficSecret secret;
  ZuCHECK(trafficSecret(secret, 0x42) &&
      link->installOneRTT(secret, secret, {}),
    "crypto retransmit key setup failed");
  link->initServerPath();
  link->validatePath();
  ZuCHECK(link->sendCryptoBytes("0123456789"),
    "crypto send setup failed");
  link->sendCryptoRef(1, Zquic::PktNumSpace::AppData, 2, 5);
  link->sendAckEliciting(4);
  link->ackOnly(1);
  link->ackOnly(4);

  Zquic::PktNumSpace::T level = Zquic::PktNumSpace::Initial;
  Zquic::SentFrameRef ref;
  Zquic::Frame frame;
  uint8_t frameBuf[Zquic::BufSize];
  ZuCHECK(link->nextRetransmitRef(level, ref) &&
      level == Zquic::PktNumSpace::AppData &&
      ref.kind == Zquic::SentFrameKind::Crypto &&
      ref.offset == 0 &&
      ref.length == 2 &&
      link->rebuildCrypto(level, ref, frame, frameBuf, sizeof(frameBuf)) &&
      frame.type == Zquic::FrameType::Crypto &&
      frame.offset == 0 &&
      frame.length == 2 &&
      frame.payload == "01",
    "crypto retransmit did not clip before ACKd hole");
  ZuCHECK(link->nextRetransmitRef(level, ref) &&
      level == Zquic::PktNumSpace::AppData &&
      ref.kind == Zquic::SentFrameKind::Crypto &&
      ref.offset == 7 &&
      ref.length == 3 &&
      link->rebuildCrypto(level, ref, frame, frameBuf, sizeof(frameBuf)) &&
      frame.type == Zquic::FrameType::Crypto &&
      frame.offset == 7 &&
      frame.length == 3 &&
      frame.payload == "789",
    "crypto retransmit did not requeue after ACKd hole");
  ZuCHECK(!link->nextRetransmitRef(level, ref),
    "crypto retransmit clipping left extra work");
  link->teardown();
}

void testCryptoRetxBudget()
{
  ZuTestScopeRT(testCryptoRetxBudget);

  App app;
  Zquic::TrafficSecret secret;
  ZQUIC_CHECK_RT(trafficSecret(secret, 0x42), "crypto retransmit secret failed");
  using Data = ZtArray<uint8_t, ZtArrayHeapID<"Zquic.Test.CryptoRetx">>;
  Data data;
  data.length(Zquic::MinUDPPayload<<1);
  memset(data.data(), 0x42, data.length());
  for (bool ack : {false, true}) {
    auto space = Zquic::PktNumSpace::Handshake;
    ZmRef<TestLink> link = testLink(&app);
    ZQUIC_CHECK_RT(link->installHandshake(secret),
      "crypto retransmit key setup failed");
    link->initServerPath();
    link->validatePath();
    ZQUIC_CHECK_RT(link->sendCryptoBytes(data, space), "crypto flight setup failed");
    if (ack) link->receiveMarked(1, Zquic::EcnMark::NotECT, true, space);

    Zquic::PktNumSpace::T level;
    Zquic::SentFrameRef ref;
    unsigned bytes = 0;
    uint64_t pn = 0;
    // A rejected retransmission must preserve data from the original flight.
    ZQUIC_CHECK_RT(link->nextCryptoPTO(level, ref), "missing crypto PTO range");
    ref.length = 32;
    ZQUIC_CHECK_RT(!link->sendRetxCrypto(level, ref, bytes, pn, true),
      "crypto retransmit rejection did not fail");
    uint64_t offset = 0;
    unsigned packets = 0;
    while (offset < data.length()) {
      bool pending = link->nextCryptoPTO(level, ref);
      ZQUIC_CHECK_RT(pending && level == space && ref.offset == offset,
	"crypto retransmit lost an unacknowledged prefix");
      if (!pending || ref.offset != offset) break;
      bool sent = link->sendRetxCrypto(level, ref, bytes, pn);
      ZQUIC_CHECK_RT(sent && bytes <= link->activePathMaxUDP(),
	"crypto retransmit exceeded path budget");
      if (!sent) break;
      offset += ref.length;
      ++packets;
      link->ackOnly(pn, level);
    }
    ZQUIC_CHECK_RT(offset == data.length() && packets > 1 &&
	!link->nextCryptoPTO(level, ref),
      "crypto retransmit did not deliver and ACK the complete flight");
    link->teardown();
  }
}

void testPTOMultipleSpaces()
{
  ZuTestScopeRT(testPTOMultipleSpaces);

  App app;
  Zquic::TrafficSecret secret;
  ZQUIC_CHECK_RT(trafficSecret(secret, 0x71), "PTO secret setup failed");
  for (bool inFlight : {false, true})
    for (bool appKeys : {false, true})
      for (bool reject : {false, true}) {
	auto link = testLink(&app);
	ZQUIC_CHECK_RT(link->installHandshake(secret) &&
	    (!appKeys || link->installAppTx(secret)), "PTO key install failed");
	ZQUIC_CHECK_RT(link->sendCryptoBytes(
	    "finished", Zquic::PktNumSpace::Handshake), "PTO flight setup failed");
	if (inFlight)
	  link->recordSentPkt(Zquic::PktNumSpace::AppData, link->sentPkts++, 100,
	    Zquic::SentFrameRef::control(), true);
	for (unsigned i = 0; i < 3; ++i) {
	  unsigned spaces[Zquic::PktNumSpace::N] = {};
	  uint64_t before = link->diag().tx.packetsTx;
	  link->probePTO(spaces, reject);
	  ZQUIC_CHECK_RT(spaces[Zquic::PktNumSpace::Handshake] == !reject &&
	      spaces[Zquic::PktNumSpace::AppData] ==
		unsigned(!reject && inFlight && appKeys) &&
	      !spaces[Zquic::PktNumSpace::Initial] &&
	      link->diag().tx.packetsTx - before <= 2,
	    "PTO omitted outstanding space or sent an unsolicited/excess probe");
	}
	link->teardown();
      }

  // A failed primary Handshake probe must not turn into an unsolicited
  // 1-RTT probe, even when that other encryption level can send.
  for (bool inFlight : {false, true}) {
    auto rejected = testLink(&app);
    ZQUIC_CHECK_RT(rejected->installHandshake(secret) &&
	rejected->installAppTx(secret) && rejected->sendCryptoBytes(
	  "finished", Zquic::PktNumSpace::Handshake),
      "rejected primary PTO setup failed");
    if (inFlight)
      rejected->recordSentPkt(
	Zquic::PktNumSpace::AppData, rejected->sentPkts++, 100,
	Zquic::SentFrameRef::control(), true);
    unsigned spaces[Zquic::PktNumSpace::N] = {};
    rejected->probePTO(spaces, false, Zquic::PktNumSpace::Handshake);
    ZQUIC_CHECK_RT(!spaces[Zquic::PktNumSpace::Initial] &&
	!spaces[Zquic::PktNumSpace::Handshake] &&
	!spaces[Zquic::PktNumSpace::AppData],
      "failed primary PTO substituted an unrelated packet-number space");
    rejected->teardown();
  }

  // Local rejection happens after packet protection/recording. Consuming a
  // packet number and incrementing packetsTx does not prove primary admission.
  auto recorded = testLink(&app);
  ZQUIC_CHECK_RT(recorded->installHandshake(secret) &&
      recorded->installAppTx(secret) && recorded->sendCryptoBytes(
	"finished", Zquic::PktNumSpace::Handshake),
    "recorded rejection PTO setup failed");
  Zquic::PktNumSpace::T recordedLevel;
  unsigned recordedProbes = 0;
  ZQUIC_CHECK_RT(recorded->forcePTOReclaimQLog(recordedLevel, recordedProbes) &&
      recordedLevel == Zquic::PktNumSpace::Handshake &&
      recorded->diag().tx.retransmitPending[Zquic::PktNumSpace::Handshake],
    "recorded rejection did not retain a Handshake retransmission");
  recorded->recordSentPkt(Zquic::PktNumSpace::AppData, recorded->sentPkts++, 100,
    Zquic::SentFrameRef::control(), true);
  unsigned recordedSpaces[Zquic::PktNumSpace::N] = {};
  uint64_t recordedPN = recorded->sentPkts;
  uint64_t recordedPackets = recorded->diag().tx.packetsTx;
  recorded->probePTO(
    recordedSpaces, false, Zquic::PktNumSpace::Handshake, true);
  ZQUIC_CHECK_RT(recorded->sentPkts == recordedPN + 1 &&
      recorded->diag().tx.packetsTx == recordedPackets + 1 &&
      recorded->rejectedProbeRecords == 1 &&
      !recordedSpaces[Zquic::PktNumSpace::Initial] &&
      !recordedSpaces[Zquic::PktNumSpace::Handshake] &&
      !recordedSpaces[Zquic::PktNumSpace::AppData],
    "recorded local rejection was mistaken for primary PTO admission");
  recorded->teardown();

  // Generic retransmission scans Initial first. Pending Initial loss work
  // must not consume the selected Handshake probe's single-packet budget.
  auto mixed = testLink(&app);
  ZQUIC_CHECK_RT(mixed->installHandshake(secret) && mixed->installAppTx(secret) &&
      mixed->sendCryptoBytes("finished", Zquic::PktNumSpace::Handshake),
    "mixed-space retained PTO setup failed");
  uint64_t initialPN = mixed->sentPkts;
  ZQUIC_CHECK_RT(mixed->sendCryptoBytes("initial", Zquic::PktNumSpace::Initial),
    "mixed-space retained Initial flight setup failed");
  uint64_t ackPN = initialPN + 3;
  mixed->recordSentPkt(Zquic::PktNumSpace::Initial, ackPN, 100,
    Zquic::SentFrameRef::control(), true);
  mixed->sentPkts = ackPN + 1;
  mixed->ackOnly(ackPN, Zquic::PktNumSpace::Initial);
  mixed->recordSentPkt(Zquic::PktNumSpace::AppData, mixed->sentPkts++, 100,
    Zquic::SentFrameRef::control(), true);
  ZQUIC_CHECK_RT(mixed->diag().tx.retransmitPending[
      Zquic::PktNumSpace::Initial] == 1,
    "mixed-space setup did not retain pending Initial loss work");
  unsigned rejectedSpaces[Zquic::PktNumSpace::N] = {};
  mixed->probePTO(rejectedSpaces, false, Zquic::PktNumSpace::Handshake);
  ZQUIC_CHECK_RT(!rejectedSpaces[Zquic::PktNumSpace::Initial] &&
      !rejectedSpaces[Zquic::PktNumSpace::Handshake] &&
      !rejectedSpaces[Zquic::PktNumSpace::AppData] &&
      mixed->diag().tx.retransmitPending[Zquic::PktNumSpace::Initial] == 1,
    "failed primary PTO emitted another space's retained CRYPTO flight");
  unsigned mixedSpaces[Zquic::PktNumSpace::N] = {};
  mixed->probePTO(mixedSpaces);
  ZQUIC_CHECK_RT(!mixedSpaces[Zquic::PktNumSpace::Initial] &&
      mixedSpaces[Zquic::PktNumSpace::Handshake] == 1 &&
      mixedSpaces[Zquic::PktNumSpace::AppData] == 1 &&
      mixed->diag().tx.retransmitPending[Zquic::PktNumSpace::Initial] == 1,
    "other-space recovery consumed the primary/companion PTO budget");
  Zquic::PktNumSpace::T pendingLevel;
  Zquic::SentFrameRef pendingRef;
  ZQUIC_CHECK_RT(mixed->nextRetransmitRef(
      pendingLevel, pendingRef, Zquic::PktNumSpace::Initial) &&
      pendingLevel == Zquic::PktNumSpace::Initial &&
      pendingRef.kind == Zquic::SentFrameKind::Crypto &&
      pendingRef.offset == 0 && pendingRef.length == 7,
    "PTO lost another space's retained recovery reference");
  mixed->teardown();

  // Confirmation retires the selected Handshake space. It must immediately
  // move the live PTO timer to outstanding AppData without another send/ACK.
  auto transition = testLink(&app);
  ZQUIC_CHECK_RT(transition->installHandshake(secret) &&
      transition->installAppTx(secret) && transition->sendCryptoBytes(
	"finished", Zquic::PktNumSpace::Handshake),
    "confirmation PTO transition setup failed");
  transition->recordSentPkt(
    Zquic::PktNumSpace::AppData, transition->sentPkts++, 100,
    Zquic::SentFrameRef::control(), true);
  auto beforeConfirm = transition->diag().tx;
  ZQUIC_CHECK_RT(beforeConfirm.ptoTimerActive &&
      beforeConfirm.sentPackets[Zquic::PktNumSpace::Handshake] &&
      beforeConfirm.pktBytesInFlight[Zquic::PktNumSpace::AppData] == 100,
    "confirmation transition did not start with both packet spaces in flight");
  transition->confirmHandshake();
  auto afterConfirm = transition->diag().tx;
  ZQUIC_CHECK_RT(afterConfirm.ptoTimerActive &&
      afterConfirm.ptoArmed == beforeConfirm.ptoArmed + 1 &&
      !afterConfirm.sentPackets[Zquic::PktNumSpace::Handshake] &&
      afterConfirm.pktBytesInFlight[Zquic::PktNumSpace::AppData] == 100,
    "Handshake retirement did not immediately rearm PTO for AppData");
  unsigned transitionSpaces[Zquic::PktNumSpace::N] = {};
  transition->probePTO(transitionSpaces);
  ZQUIC_CHECK_RT(transitionSpaces[Zquic::PktNumSpace::AppData] == 1 &&
      !transitionSpaces[Zquic::PktNumSpace::Handshake],
    "confirmed PTO still selected the retired Handshake space");
  transition->teardown();

  // 1-RTT keys and outstanding output alone must not arm an AppData PTO.
  auto client = testLink(&app);
  ZQUIC_CHECK_RT(client->installAppTx(secret), "App-only PTO key setup failed");
  client->recordSentPkt(Zquic::PktNumSpace::AppData, client->sentPkts++, 100,
    Zquic::SentFrameRef::control(), true);
  unsigned spaces[Zquic::PktNumSpace::N] = {};
  client->probePTO(spaces);
  ZQUIC_CHECK_RT(!spaces[Zquic::PktNumSpace::AppData] &&
      !client->diag().tx.ptoTimerActive,
    "unconfirmed client independently probed AppData");
  uint64_t armed = client->diag().tx.ptoArmed;
  client->confirmHandshake();
  ZQUIC_CHECK_RT(client->diag().tx.ptoTimerActive &&
      client->diag().tx.ptoArmed == armed + 1,
    "confirmation did not immediately arm previously blocked AppData PTO");
  client->probePTO(spaces);
  ZQUIC_CHECK_RT(spaces[Zquic::PktNumSpace::AppData] == 1,
    "confirmed client did not resume AppData PTO");
  client->teardown();
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
#ifdef Zquic_DEBUG
  link->forcePN(40000);
  ZuCHECK(link->pnLength() == 3,
    "runtime PN length did not grow to three bytes");
  link->forcePN(9000000);
  ZuCHECK(link->pnLength() == 4,
    "runtime PN length did not grow to four bytes");
#endif
  link->closeForTest();
}

void testLongHeaderCoalescing()
{
  ZuTestScope(testLongHeaderCoalescing);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  unsigned sends = 0;
  unsigned bytes = 0;
  ZuCHECK(link->coalesceProbe(sends, bytes) &&
      sends == 1 &&
      bytes == 140,
    "Initial/Handshake coalescing did not emit one combined datagram");
  link->closeForTest();
}

static void ackCreditServicesRetx(bool pending)
{
  ZuTestScopeRT(ackCreditServicesRetx);

  App app;
  app.deferredTx = true;
  auto link = testLink(&app);
  auto stream = link->stream(Zquic::StreamType::Duplex);
  auto retained = app.streamTxAllocator()->allocBuf();
  retained->append(reinterpret_cast<const uint8_t *>("retained"), 8);
  RecoveryService service;
  service.origin = retained;
  bool setup = false, kept = false, released = false;
  app.onTx([&link, &stream, &retained, &service, &setup, pending]() {
    link->grantDataCredit(8);
    stream->txCredit(8);
    {
      auto out = stream->txStream_();
      out.appendBuf(retained);
      out.flush();
    }
    // Record real retained STREAM references, then declare only that packet
    // lost through the real ACK packet-threshold path. No synthetic lost queue.
    bool sent = link->flushCongestedStream(stream);
    if (pending) {
      link->sendAckEliciting(4, 100);
      link->ackOnly(4);
      link->sentPkts = 5;
    }
    link->fillCwnd();
    setup = sent && stream->txStillUnackd(0, 8) &&
      link->diag().tx.retransmitPending[Zquic::PktNumSpace::AppData] ==
        unsigned(pending) && !link->congestionAllowance();
    link->service = &service;
    if (pending) {
      // Open credit without newly detecting loss: pre-existing pending
      // recovery must still be serviced on the ACK owner.
      link->ackOnly(5);
    } else {
      // Also exercise the newly-lost notification. Its real deferred owner
      // task must not lose all credit to a synchronous ordinary flush.
      link->ackRange(3, link->sentPkts - 1);
    }
  });
  app.drainTx(); // includes any genuinely deferred recovery notification
  app.onTx([&link, &stream, &service, &kept, &released]() {
    kept = stream->txStillUnackd(0, 8) && service.valid;
    link->service = nullptr;
    if (service.recovered) link->ackOnly(service.recoveredPN);
    released = service.recovered && !stream->txStillUnackd(0, 8);
  });
  link->teardown();
  ZQUIC_CHECK_RT(setup, "retained STREAM and closed congestion setup failed");
  ZQUIC_CHECK_RT(service.recovered == 1 && service.events >= 2 &&
      service.order[0] == 'R' && service.order[1] == 'N',
    "ACK-reopened credit went to bulk before already-pending recovery");
  ZQUIC_CHECK_RT(!service.blocked && !service.queued,
    "recovery was deferred behind the synchronous bulk flush");
  ZQUIC_CHECK_RT(kept && released,
    "retained STREAM backing/ACK ownership changed while servicing recovery");
}

void congestionBudgetGatesSends()
{
  ZuTestScope(congestionBudgetGatesSends);

#ifdef Zquic_DEBUG
  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zquic::StreamType::Duplex);
  link->grantDataCredit(20000);
  stream->txCredit(20000);
  {
    auto tx = stream->txStream_();
    tx << "0123456789abcdef0123456789abcdef" << Zi::flush();
  }
  stream->fin();

  link->fillCwnd();
  Zquic::LinkDiag diag = link->diag();
  ZuCHECK(diag.tx.bytesInFlight >= diag.tx.congestionWindow &&
      !link->flushCongestedStream(stream) &&
      stream->txBufferedBytes(),
    "runtime stream send bypassed closed congestion window");

  ZuCHECK(!link->congestionAllowance(),
    "runtime congestion allowance remained open after cwnd fill");
  ZuCHECK(link->queuePathResponse(), "runtime control frame queue failed");
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

static void testStreamBatchContinuation()
{
  ZuTestScope(testStreamBatchContinuation);

#ifdef Zquic_DEBUG
  App app;
  ZmRef<TestLink> link = testLink(&app);
  using Data = ZtArray<uint8_t, ZtArrayHeapID<"Zquic.Test.StreamBatch">>;
  Data data;
  data.length((TestLink::StreamFlushBatch + 1) * Zquic::BufSize);
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
  auto stream = link->stream(Zquic::StreamType::Duplex);
  link->grantDataCredit(data.length());
  stream->txCredit(data.length());
  {
    auto tx = stream->txStream_();
    tx << data;
    tx.flush();
  }
  stream->fin();
  unsigned queued = link->txFlushQueued;
  uint64_t before = link->sentPkts;
  ZuCHECK(link->flushStreamBatch() &&
      link->sentPkts == before + TestLink::StreamFlushBatch &&
      link->scheduledStreams() && stream->txBufferedBytes() &&
      link->txFlushQueued == queued + 1,
    "productive full batch must yield and schedule only its own remaining output");
  queued = link->txFlushQueued;
  ZuCHECK(link->flushStreamBatch() && !link->scheduledStreams() &&
      !stream->txBufferedBytes() && link->txFlushQueued == queued,
    "final partial batch must drain without another continuation");
  link->teardown();

  for (bool congestion : {false, true}) {
    ZmRef<TestLink> blocked = testLink(&app);
    auto pending = blocked->stream(Zquic::StreamType::Duplex);
    blocked->grantDataCredit(100);
    pending->txCredit(congestion ? 100 : 0);
    {
      auto tx = pending->txStream_();
      tx << "blocked";
      tx.flush();
    }
    if (congestion) blocked->fillCwnd();
    // A flow-blocked producer may emit its one STREAM_DATA_BLOCKED notice.
    // Once that notice is sent, only new credit can make further progress.
    else (void)blocked->flushStreamBatch();
    queued = blocked->txFlushQueued;
    before = blocked->sentPkts;
    (void)blocked->flushStreamBatch();
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

#ifdef Zquic_DEBUG
  {
    App app{1360};
    ZmRef<TestLink> client = testLink(&app);
    client->growActivePath(1360);
    Zquic::PktBudget budget = client->sendBudget();
    ZuCHECK(client->pathValidated() &&
	client->activePathMaxUDP() == 1360 &&
	budget.pmtu == 1360 - Zquic::TxStreamPktReserve &&
	budget.limit() == 1360 - Zquic::TxStreamPktReserve,
      "active path max UDP did not drive runtime packet budget");
    client->closeForTest();
  }
  Zi::Path path = testPath("ZquicStreamPathBudgetQLog.sqlog");
  ZiFile::remove(path);
	ZquicLogParams params;
	params.enabled(true).path(path).
	  ringSize(1<<15);
#endif

	App app;
#ifdef Zquic_DEBUG
	ZuCHECK(ZquicLogger::init(
	    app.qlogTrace(), params, Zquic::Vantage::Unknown),
	  "path-budget qlog init failed");
	ZquicLogger::start();
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

  Zquic::PathDiag diag = server->pathDiag();
  ZuCHECK(diag.bytesRx == 400 && diag.bytesTx == 1200,
    "active path byte accounting mismatch after budget exhaustion");

  server->pathReceived(100);
  ZuCHECK(server->pathAntiAmp() == 300 &&
      server->pathSend(300),
    "received bytes did not expand server anti-amplification budget");

  server->validatePath();
  ZuCHECK(server->pathValidated() &&
      ZuCmp<uint64_t>::null(server->pathAntiAmp()) &&
      server->pathSend(Zquic::MinUDPPayload),
    "address validation did not unlock active path send allowance");
  server->closeForTest();

#ifdef Zquic_DEBUG
	closeQLog(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag qdiag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());
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

void passivePathChallengeRecovery()
{
  ZuTestScope(passivePathChallengeRecovery);

#ifdef Zquic_DEBUG
  App app;
  auto link = testLink(&app, true);
  ZiSockAddr local{ip4_(0x0a000001), 4433};
  ZiSockAddr oldRemote{ip4_(0x0a000002), 50000};
  ZiSockAddr newRemote{ip4_(0x0a000002), 50001};
  {
    auto blocked = testLink(&app, true);
    blocked->confirmHandshake();
    blocked->initServerPath(local, oldRemote);
    blocked->validatePath();
    auto older = blocked->stream(Zquic::StreamType::Duplex);
    auto tx = older->txStream_();
    tx << "older-a" << Zi::flush();
    tx << "older-b" << Zi::flush();
    for (unsigned pn = 0; pn < 2; ++pn) {
      Zquic::TxRange range;
      bool dequeued = older->dequeueTxRange(range);
      ZuCHECK(dequeued && range.buf,
	"old-path PTO setup did not retain its actual STREAM backing");
      if (!dequeued) break;
      blocked->recordSentPkt(Zquic::PktNumSpace::AppData, pn,
	range.length + 32, Zquic::SentFrameRef::stream(
	  uint64_t(older->id()), range, false), true);
    }
    // Existing old-path loss work stays ahead of the new validation probe in
    // the retransmit queue, even when PTO reclaims newer packet references.
    blocked->sendAckEliciting(4);
    blocked->ackOnly(4);
    blocked->sentPkts = 5;
    blocked->fillCwnd();
    blocked->directPassive = true;
    blocked->observeCandidate(local, newRemote, 48);
    Zquic::PathData token{blocked->validatingChallenge()};
    unsigned oldCalls = blocked->retransmitCalls;
    uint64_t ptoCount = blocked->diag().tx.ptoCount;
    ZuCHECK(!blocked->congestionAllowance() && blocked->passiveSends == 1,
      "old-path congestion must be full before the lost candidate proof");
    blocked->recoverPTO();
    ZuCHECK(blocked->passiveSends == 2 && blocked->passiveRemote == newRemote &&
	blocked->passiveData == token && blocked->retransmitCalls == oldCalls &&
	blocked->validatingPath() && !blocked->candidateValidated() &&
	blocked->candidateAntiAmp() == 48 * 3 - 2 * 44 &&
	blocked->diag().tx.ptoCount == ptoCount + 1 &&
	older->txUnackdBytes() == 14,
      "PTO must retry candidate proof before old STREAMs while retaining bytes "
      "and respecting the candidate amplification budget");
    Zquic::PktNumSpace::T level;
    Zquic::SentFrameRef ref;
    ZuCHECK(blocked->nextRetransmitRef(level, ref) &&
	ref.kind == Zquic::SentFrameKind::Stream && ref.range.buf,
      "PTO did not retain its older STREAM recovery work");
    ZuCHECK(blocked->pathResponse(token.cspan()) &&
	blocked->activePathRemote() == newRemote,
      "retried candidate proof did not permit authenticated path promotion");
    blocked->teardown();
  }
  link->initServerPath(local, oldRemote);
  link->validatePath();
  link->directPassive = true;
  link->observeCandidate(local, newRemote, 48);
  Zquic::PathData challenge{link->validatingChallenge()};
  ZuCHECK(link->validatingPath() && !link->candidateValidated() &&
      link->passiveSends == 1 && link->passiveRemote == newRemote &&
      !link->queuedControlFrames(),
    "direct passive challenge must retain its validation proof after send");

  link->observeCandidate(local, newRemote, 48);
  ZuCHECK(link->validatingPath() && !link->candidateValidated() &&
      link->validatingChallenge() == challenge.cspan() &&
      link->activePathRemote() == oldRemote && link->passiveSends == 1,
    "another packet from a candidate must not validate it or retire its challenge");

  // Drop the first challenge. A later acknowledged packet makes recovery
  // declare its packet lost; its exact token must remain retryable.
  link->sendAckEliciting(4);
  link->ackOnly(4);
  Zquic::PktNumSpace::T level = Zquic::PktNumSpace::Initial;
  Zquic::SentFrameRef ref;
  Zquic::Frame frame;
  Zquic::PathData retry;
  ZuCHECK(link->nextRetransmitRef(level, ref) &&
      level == Zquic::PktNumSpace::AppData &&
      link->rebuildControl(ref, frame, &retry) &&
      frame.type == Zquic::FrameType::PathChallenge &&
      retry.cspan() == challenge.cspan(),
    "lost direct passive challenge must remain tracked by actual packet recovery");
  link->sentPkts = 5;
  ZuCHECK(link->sendPassiveMigChal() && link->passiveSends == 2 &&
      link->passiveRemote == newRemote && link->validatingPath() &&
      !link->candidateValidated(),
    "challenge retry must preserve the candidate tuple and pending proof");
  ZuCHECK(link->pathResponse(challenge.cspan()) &&
      !link->validatingPath() && link->pathValidated() &&
      link->activePathRemote() == newRemote,
    "only the matching response may promote and validate the candidate");

  link->sendAckEliciting(9);
  link->ackOnly(9);
  ZuCHECK(!link->nextRetransmitRef(level, ref),
    "validated challenge must not remain eligible for recovery");
  link->teardown();
#endif
}

void pathValidationStateMachine()
{
  ZuTestScope(pathValidationStateMachine);

  Zquic::PathChallenge challenge;
  ZuCHECK(challenge.generate() &&
      challenge.valid() &&
      challenge.length() == Zquic::PathChallenge::Length &&
      challenge.equals(challenge.bspan()),
    "PATH_CHALLENGE value generation failed");

#ifdef Zquic_DEBUG
  Zi::Path path = testPath("ZquicStreamPathValidationQLog.sqlog");
  ZiFile::remove(path);
	ZquicLogParams params;
	params.enabled(true).path(path).
	  ringSize(1<<15);

	App app;
	ZuCHECK(ZquicLogger::init(
	    app.qlogTrace(), params, Zquic::Vantage::Unknown),
	  "path-validation qlog init failed");
	ZquicLogger::start();
	ZmRef<TestLink> link = testLink(&app, true);
  ZiSockAddr local{ip4_(0x0a000001), 4433};
  ZiSockAddr oldRemote{ip4_(0x0a000002), 50000};
  ZiSockAddr newRemote{ip4_(0x0a000002), 50001};
  ZiSockAddr otherRemote{ip4_(0x0a000002), 50002};

  link->initServerPath(local, oldRemote);
  link->validatePath();
  ZuCHECK(link->activePathRemote() == oldRemote,
    "active path remote setup failed");

  link->observePath(local, newRemote);
  ZuBSpan data = link->validatingChallenge();
  uint8_t response[Zquic::PathChallenge::Length]{};
  memcpy(response, data.data(), data.length());
  ZuCHECK(link->validatingPath() &&
      data.length() == Zquic::PathChallenge::Length &&
      link->queuedControlFrames() == 1 &&
      link->activePathRemote() == oldRemote,
    "new path replaced active remote before validation");

  uint8_t bad[Zquic::PathChallenge::Length]{};
  bad[0] = response[0] ^ 0xffU;
  ZuCHECK(!link->pathResponse(ZuBSpan{bad, sizeof(bad)}) &&
      link->validatingPath() &&
      link->activePathRemote() == oldRemote,
    "unmatched PATH_RESPONSE changed path-validation state");

  link->observePath(local, newRemote);
  ZuCHECK(link->queuedControlFrames() == 1,
    "repeated packet from validating path queued duplicate challenge");

  ZuCHECK(link->pathResponse(ZuBSpan{response, sizeof(response)}) &&
      !link->validatingPath() &&
      link->pathValidated() &&
      link->activePathRemote() == newRemote,
    "matching PATH_RESPONSE did not promote candidate path");

  link->observePath(local, otherRemote);
  ZuCHECK(link->validatingPath() &&
      link->activePathRemote() == newRemote,
    "second candidate setup failed");
  link->pathTimeout();
  ZuCHECK(!link->validatingPath() &&
      link->activePathRemote() == newRemote,
    "path-validation timeout did not retain active path");

  link->closeForTest();

	closeQLog(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());
  ZuCHECK(diag.recordsEnqueued >= 6,
    "path-validation qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 7,
    "path-validation qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0,
    "path-validation qlog writer failure");

  ZtString<> qlog = readFile_(path);
  ZuCHECK(qlog, "path-validation qlog output missing");
  ZuCHECK(parseJSONSeq(qlog) >= 7,
    "path-validation qlog JSON-SEQ parse failed");
  ZuCHECK(qlog.find<"quic:path_validated">() >= 0,
    "path-validation qlog event missing");
  ZuCHECK(qlog.find<"\"success\":true">() >= 0,
    "path-validation qlog success missing");
  ZuCHECK(qlog.find<"\"success\":false">() >= 0,
    "path-validation qlog failure missing");
  ZuCHECK(qlog.find<"\"vantage\":\"unknown\"">() >= 0,
    "path-validation qlog vantage point missing");
#endif
}

void testPMTUDQLog()
{
  ZuTestScope(testPMTUDQLog);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath("ZquicStreamPMTUDQLog.sqlog");
	ZiFile::remove(path);
	ZquicLogParams params;
	params.enabled(true).path(path).ringSize(1<<15);

	App app{Zquic::BufSize};
	ZuCHECK(ZquicLogger::init(
	    app.qlogTrace(), params, Zquic::Vantage::Unknown),
	  "PMTUD qlog init failed");
	ZquicLogger::start();
	ZmRef<TestLink> link = testLink(&app, true);
  link->initServerPath(
    ZiSockAddr{ip4_(0x0a000001), 4433},
    ZiSockAddr{ip4_(0x0a000002), 50000});
  link->validatePath();
  ZuCHECK(link->pathValidated(), "PMTUD qlog path validation setup failed");

  ZuCHECK(link->startPMTUDProbe(1400),
    "PMTUD qlog ACK probe setup failed");
  link->ackPMTUDProbe(1400);
  ZuCHECK(link->activePathMaxUDP() == 1400,
    "PMTUD qlog ACK did not grow active path");

  ZuCHECK(link->startPMTUDProbe(1500),
    "PMTUD qlog loss probe setup failed");
  link->losePMTUDProbe(1500);
  link->closeForTest();

	closeQLog(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());
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

#ifdef Zquic_DEBUG
void testCIDQLog()
{
  ZuTestScope(testCIDQLog);

  Zi::Path path = testPath("ZquicStreamCIDQLog.sqlog");
  ZiFile::remove(path);

	ZquicLogParams params;
	params.enabled(true).path(path).ringSize(1<<15);

	App app;
	ZuCHECK(ZquicLogger::init(
	    app.qlogTrace(), params, Zquic::Vantage::Unknown),
	  "CID qlog init failed");
	ZquicLogger::start();
	ZmRef<TestLink> link = testLink(&app);
  Zquic::ResetToken token{"0123456789abcdef"};
  Zquic::CxnID localCID{"localcid"};
  ZuCHECK(link->addLocalCID(localCID, 1, token),
    "CID qlog local issue setup failed");

  TestCIDRoutes routes;
  link->installLocalCIDRoutes(routes);
  ZuCHECK(routes.adds >= 1, "CID qlog route install did not bind route");

  Zquic::Frame retire;
  retire.type = Zquic::FrameType::RetireCxnID;
  retire.value = 1;
  ZuCHECK(link->rxRetireCxnID(retire),
    "CID qlog retire setup failed");

  Zquic::CxnID peerCID0{"peercid0"};
  Zquic::CxnID peerCID1{"peercid1"};
  Zquic::ResetToken token1{"123456789abcdef0"};
  Zquic::Frame peer;
  peer.type = Zquic::FrameType::NewCxnID;
  peer.value = 0;
  peer.length = peerCID0.length();
  peer.payload = peerCID0;
  peer.resetToken = token;
  ZuCHECK(link->rxNewCxnID(peer),
    "CID qlog peer CID 0 setup failed");
  peer.value = 1;
  peer.offset = 1;
  peer.length = peerCID1.length();
  peer.payload = peerCID1;
  peer.resetToken = token1;
  ZuCHECK(link->rxNewCxnID(peer),
    "CID qlog peer CID retire-prior-to setup failed");

  link->closeForTest();

	closeQLog(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());

  ZuCHECK(diag.recordsEnqueued >= 5, "CID qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 6, "CID qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "CID qlog writer failure");

  ZtString<> qlog = readFile_(path);
  ZuCHECK(qlog, "CID qlog output missing");
  ZuCHECK(parseJSONSeq(qlog) >= 6, "CID qlog JSON-SEQ parse failed");
  ZuCHECK(qlog.find<"quic:connection_id_updated">() >= 0,
    "CID qlog event missing");
  ZuCHECK(qlog.find<"\"initiator\":\"local\"">() >= 0,
    "CID qlog missing local initiator");
  ZuCHECK(qlog.find<"\"initiator\":\"remote\"">() >= 0,
    "CID qlog missing remote initiator");
  ZuCHECK(qlog.find<"\"new\":\"6C6F63616C636964\"">() >= 0,
    "CID qlog missing issued local CID");
  ZuCHECK(qlog.find<"\"old\":\"6C6F63616C636964\"">() >= 0,
    "CID qlog missing retired local CID");
  ZuCHECK(qlog.find<"\"new\":\"7065657263696430\"">() >= 0 &&
      qlog.find<"\"old\":\"7065657263696430\"">() >= 0 &&
      qlog.find<"\"new\":\"7065657263696431\"">() >= 0,
    "CID qlog missing peer issue/retire sequence");
}

void testAckECNValidateDisable()
{
  ZuTestScope(testAckECNValidateDisable);

  Zi::Path path = testPath("ZquicStreamECNQLog.sqlog");
	ZiFile::remove(path);
	ZquicLogParams params;
	params.enabled(true).path(path).ringSize(1<<15);

	App app;
	ZuCHECK(ZquicLogger::init(
	    app.qlogTrace(), params, Zquic::Vantage::Unknown),
	  "ECN qlog init failed");
	ZquicLogger::start();
	ZmRef<TestLink> link = testLink(&app);
  link->enableECN();
  ZuCHECK(!link->ecnDisabled(), "ECN did not enable for validation test");
  ZuCHECK(link->receiveMarked(1, Zquic::EcnMark::ECT0) &&
      link->receiveMarked(2, Zquic::EcnMark::CE),
    "runtime ECN receive marking failed");
  Zquic::LinkDiag diag = link->diag();
  ZuCHECK(diag.rx.ecnRx[Zquic::PktNumSpace::AppData].ect0 == 1 &&
      diag.rx.ecnRx[Zquic::PktNumSpace::AppData].ce == 1,
    "runtime ECN receive diagnostics mismatch");
  Zquic::PktBuild build;
  ZuCHECK(link->writePendingAck(build) &&
      build.count() == 1 &&
      build.data()[0].len > 0,
    "runtime pending ACK_ECN build failed");
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuBSpan{build.data()[0].base, unsigned(build.data()[0].len)},
      frame, used) &&
      frame.type == Zquic::FrameType::Ack &&
      frame.ackECN.ect0 == 1 &&
      frame.ackECN.ce == 1,
    "runtime received ECN marks were not emitted in ACK_ECN");

  link->sendECN(0, Zquic::EcnMark::ECT0);
  link->sendECN(1, Zquic::EcnMark::ECT0);
  link->sendECN(2, Zquic::EcnMark::ECT1);
  link->ackECN(2, 2, 1, 0);
  diag = link->diag();
  ZuCHECK(!link->ecnDisabled() &&
      diag.tx.peerAckECN[Zquic::PktNumSpace::AppData].ect0 == 2 &&
      diag.tx.peerAckECN[Zquic::PktNumSpace::AppData].ect1 == 1,
    "valid ACK_ECN did not update runtime diagnostics");
  link->ackECN(2, 1, 1, 0);
  diag = link->diag();
  ZuCHECK(link->ecnDisabled() && diag.tx.ecnValidationFailures == 1,
    "regressing ACK_ECN did not disable ECN");
  link->closeForTest();

  ZmRef<TestLink> missing = testLink(&app);
  missing->enableECN();
  for (unsigned i = 0; i < Zquic::Path::ECNProbeThreshold - 1; ++i) {
    missing->sendECN(i, Zquic::EcnMark::ECT0);
    missing->ackNoECN(i);
  }
  diag = missing->diag();
  ZuCHECK(!missing->ecnDisabled() && diag.tx.ecnValidationFailures == 0,
    "missing ACK_ECN failed before probe budget expired");
  missing->sendECN(
    Zquic::Path::ECNProbeThreshold - 1, Zquic::EcnMark::ECT0);
  missing->ackNoECN(Zquic::Path::ECNProbeThreshold - 1);
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
  ce->sendECN(0, Zquic::EcnMark::ECT0);
  ce->sendECN(1, Zquic::EcnMark::ECT0);
  diag = ce->diag();
  uint64_t cwnd = diag.tx.congestionWindow;
  ce->ackECN(1, 1, 0, 1);
  diag = ce->diag();
  Zquic::PathDiag pathDiag = ce->pathDiag();
  ZuCHECK(!ce->ecnDisabled() &&
      diag.tx.peerAckECN[Zquic::PktNumSpace::AppData].ce == 1 &&
      diag.tx.congestionWindow < cwnd &&
      pathDiag.ecnCEEvents == 1 &&
      pathDiag.ecnCEBytes == 2400,
    "CE ACK_ECN did not drive congestion response and path diagnostics");
  ce->closeForTest();

	closeQLog(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag qdiag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());

  ZuCHECK(qdiag.recordsEnqueued >= 3, "ECN qlog enqueue mismatch");
  ZuCHECK(qdiag.recordsWritten >= 4, "ECN qlog write mismatch");
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

  ZmRef<TestLink> streamLink = testLink(&app);
  auto stream = streamLink->stream(Zquic::StreamType::Duplex);
  ZuCHECK(stream &&
      streamLink->queueDataBlocked(uint64_t(stream->id()), 4096) &&
      !streamLink->queueDataBlocked(uint64_t(stream->id()), 4096) &&
      streamLink->queuedControlFrames() == 1,
    "pending STREAM_DATA_BLOCKED duplicate was queued");
  (void)streamLink->flushControlSends();
  ZuCHECK(
      !streamLink->queuedControlFrames() &&
      !streamLink->queueDataBlocked(uint64_t(stream->id()), 4096) &&
      streamLink->queueDataBlocked(uint64_t(stream->id()), 8192) &&
      streamLink->queuedControlFrames() == 1,
    "sent STREAM_DATA_BLOCKED duplicate suppression mismatch");

  ZmRef<TestLink> limitLink = testLink(&app);
  limitLink->setPeerStreamLimit(Zquic::StreamType::Duplex, 0);
  (void)limitLink->stream(Zquic::StreamType::Duplex);
  ZuCHECK(limitLink->queuedControlFrames() == 1 &&
      !limitLink->queueStreamsBlocked(Zquic::StreamType::Duplex, 0) &&
      limitLink->queuedControlFrames() == 1,
    "pending STREAMS_BLOCKED duplicate was queued");
  (void)limitLink->flushControlSends();
  ZuCHECK(
      !limitLink->queuedControlFrames() &&
      limitLink->queueStreamsBlocked(Zquic::StreamType::Duplex, 0) &&
      limitLink->queuedControlFrames() == 1,
    "sent STREAMS_BLOCKED was not requeued");
  (void)limitLink->flushControlSends();
  ZuCHECK(
      !limitLink->queuedControlFrames() &&
      limitLink->queueStreamsBlocked(Zquic::StreamType::Duplex, 8) &&
      limitLink->queuedControlFrames() == 1,
    "sent STREAMS_BLOCKED duplicate suppression mismatch");
}

void testKeyedControlReplacement()
{
  ZuTestScopeRT(testKeyedControlReplacement);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  ZQUIC_CHECK_RT(link->queueMaxData(1024) &&
      !link->queueMaxData(1024) &&
      link->queueMaxData(2048) &&
      link->queuedControlFrames() == 1,
    "MAX_DATA keyed replacement mismatch");

  ZmRef<TestLink> streams = testLink(&app);
  ZQUIC_CHECK_RT(streams->queueStreamsBlocked(Zquic::StreamType::Duplex, 7) &&
      !streams->queueStreamsBlocked(Zquic::StreamType::Duplex, 7) &&
      streams->queueStreamsBlocked(Zquic::StreamType::Duplex, 8) &&
      streams->queuedControlFrames() == 1,
    "STREAMS_BLOCKED keyed replacement mismatch");

  ZmRef<TestLink> path = testLink(&app);
  for (unsigned i = 0; i < TestLink::PathResponseMax + 4; ++i)
    ZQUIC_CHECK_RT(path->queuePathResponse(i + 1), "PATH_RESPONSE queue failed");
  ZQUIC_CHECK_RT(path->queuedControlFrames() ==
      TestLink::PathResponseMax,
    "PATH_RESPONSE FIFO exceeded retention cap");
}

void testControlInvalidation()
{
  ZuTestScope(testControlInvalidation);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zquic::StreamType::Duplex);
  ZuCHECK(stream &&
      link->queueMaxStreamData(
	uint64_t(stream->id()), stream->rxCreditLimit()) &&
      link->queuedControlFrames() == 1,
    "MAX_STREAM_DATA control setup failed");
  uint8_t b[128];
  Zquic::Frame frame;
  int n = Zquic::FrameCodec::writeResetStream(
    b, sizeof(b), uint64_t(stream->id()), 7, 0);
  ZuCHECK(parseFrame(b, n, frame) && link->receiveFrame(frame) == 0,
    "stream-control invalidation setup failed");
  Zquic::SentFrameRef refs[4];
  ZuCHECK(!link->flushRecordedRefs(refs, 4) &&
      !link->queuedControlFrames(),
    "stale stream control was not cleared at invalidation");

  ZmRef<TestLink> blocked = testLink(&app);
  ZuCHECK(blocked->queueDataBlocked(1024) &&
      blocked->queuedControlFrames() == 1,
    "DATA_BLOCKED setup failed");
  blocked->grantDataCredit(2048);
  ZuCHECK(!blocked->queuedControlFrames(),
    "DATA_BLOCKED was not cleared when peer credit arrived");
}

void testPeerStreamAcceptance()
{
  ZuTestScope(testPeerStreamAcceptance);

  App app;
  ZmRef<TestLink> server = testLink(&app, true);

  Zquic::Frame frame;
  unsigned used = 0;
  auto packet = streamPkt(0, 0, "req", true, frame, used);
  ZuCHECK(packet, "client STREAM frame setup failed");

  Zquic::BufDiag diag;
  ZuCHECK(server->receiveFrame(frame, packet, &diag) == 0,
    "server failed to receive peer STREAM frame");
  auto accepted = server->findStream(0);
  ZuCHECK(accepted && server->streamedCount == 1 &&
      server->lastStream == accepted &&
      accepted->processed == 1 &&
      accepted->rxComplete() &&
      accepted->finalSize() == 3,
    "peer stream acceptance state mismatch");

  ZmRef<TestLink> client = testLink(&app);
  auto local = client->stream(Zquic::StreamType::Duplex);
  packet = streamPkt(local->id(), 0, "rsp", true, frame, used);
  ZuCHECK(packet,
    "response STREAM frame setup failed");
  ZuCHECK(client->receiveFrame(frame, packet, &diag) == 0 &&
      !client->streamedCount &&
      local->processed == 1 &&
      local->rxComplete(),
    "existing local bidi stream receive mismatch");

  packet = streamPkt(1, 0, "bad", false, frame, used);
  ZuCHECK(packet,
    "local-origin STREAM setup failed");
  ZuCHECK(server->receiveFrame(frame, packet, &diag) < 0 && !server->findStream(1),
    "server accepted local-origin peer stream ID");

  ZmRef<TestLink> gap = testLink(&app, true);
  gap->setLocalStreamLimit(Zquic::StreamType::Duplex, 3);
  packet = streamPkt(8, 0, "hi", true, frame, used);
  ZuCHECK(packet && gap->receiveFrame(frame, packet) == 0 &&
      gap->peerStreamsOpened(Zquic::StreamType::Duplex) == 3 &&
      gap->findStream(8),
    "higher peer stream was not accepted");
  packet = streamPkt(4, 0, "lo", true, frame, used);
  ZuCHECK(packet && gap->receiveFrame(frame, packet) == 0,
    "implicit lower peer stream was rejected after higher stream");
  auto lower = gap->findStream(4);
  ZuCHECK(lower && lower->processed == 1 && lower->rxComplete() &&
      !gap->diag().rx.closedStreamFrames &&
      !gap->diag().rx.invalidStreamFrames,
    "implicit lower peer stream was treated as closed or invalid");
}

void testStreamCountLimits()
{
  ZuTestScope(testStreamCountLimits);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  client->setPeerStreamLimit(Zquic::StreamType::Duplex, 1);

  auto first = client->stream(Zquic::StreamType::Duplex);
  ZuCHECK(first && first->id() == 0 &&
      client->localStreamsOpened(Zquic::StreamType::Duplex) == 1 &&
      client->streamCount() == 1,
    "first stream under peer limit did not open");
  auto blocked = client->stream(Zquic::StreamType::Duplex);
  ZuCHECK(!blocked &&
      client->localStreamsBlocked(Zquic::StreamType::Duplex) &&
      client->queuedLocalStreams(Zquic::StreamType::Duplex) == 1 &&
      client->streamCount() == 1,
    "stream over peer limit was not queued");

  uint8_t b[32];
  int n = Zquic::FrameCodec::writeMaxStreams(
    b, sizeof(b), Zquic::StreamType::Duplex, 2);
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used),
    "MAX_STREAMS setup failed");
  ZuCHECK(client->applyMaxStreams(frame) &&
      !client->queuedLocalStreams(Zquic::StreamType::Duplex) &&
      client->localStreamsOpened(Zquic::StreamType::Duplex) == 2 &&
      client->streamCount() == 2 &&
      client->streamedCount == 1 &&
      client->lastStream &&
      client->lastStream->id() == 4,
    "MAX_STREAMS did not open queued local stream");

  ZmRef<TestLink> server = testLink(&app, true);
  server->setLocalStreamLimit(Zquic::StreamType::Duplex, 1);
  auto packet = streamPkt(0, 0, "a", true, frame, used);
  ZuCHECK(packet,
    "first peer STREAM setup failed");
  ZuCHECK(server->receiveFrame(frame, packet) == 0 &&
      server->peerStreamsOpened(Zquic::StreamType::Duplex) == 1 &&
      server->findStream(0),
    "first peer stream under local limit did not open");

  packet = streamPkt(4, 0, "b", true, frame, used);
  ZuCHECK(packet,
    "second peer STREAM setup failed");
  ZuCHECK(server->receiveFrame(frame, packet) < 0 &&
      server->peerStreamsOpened(Zquic::StreamType::Duplex) == 1 &&
      !server->findStream(4),
    "peer stream count limit was not enforced");

  server->setLocalStreamLimit(Zquic::StreamType::Duplex, 2);
  ZuCHECK(server->receiveFrame(frame, packet) == 0 &&
      server->peerStreamsOpened(Zquic::StreamType::Duplex) == 2 &&
      server->findStream(4),
    "extended local stream count did not admit peer stream");

  server->setLocalStreamLimit(Zquic::StreamType::Simplex, 1);
  packet = streamPkt(2, 0, "u", true, frame, used);
  ZuCHECK(packet,
    "first peer uni STREAM setup failed");
  ZuCHECK(server->receiveFrame(frame, packet) == 0 &&
      server->peerStreamsOpened(Zquic::StreamType::Simplex) == 1 &&
      server->findStream(2),
    "first peer uni stream under local limit did not open");

  packet = streamPkt(6, 0, "v", true, frame, used);
  ZuCHECK(packet,
    "second peer uni STREAM setup failed");
  ZuCHECK(server->receiveFrame(frame, packet) == 0 &&
      server->peerStreamsOpened(Zquic::StreamType::Simplex) == 2 &&
      server->localStreamLimit(Zquic::StreamType::Simplex) == 3 &&
      server->findStream(6),
    "completed peer uni stream did not return stream-count credit");

  ZmRef<TestLink> duplexServer = testLink(&app, true);
  duplexServer->setLocalStreamLimit(Zquic::StreamType::Duplex, 1);
  packet = streamPkt(0, 0, "req", true, frame, used);
  ZuCHECK(packet && duplexServer->receiveFrame(frame, packet) == 0,
    "peer bidi stream receive setup failed");
  auto peerDuplex = duplexServer->findStream(0);
  ZuCHECK(peerDuplex && duplexServer->localStreamLimit(Zquic::StreamType::Duplex) == 1,
    "peer bidi stream returned stream-count credit before local FIN");
  unsigned queuedControls = duplexServer->queuedControlFrames();
  duplexServer->initServerPath();
  duplexServer->pathReceived(1200);
  duplexServer->grantDataCredit(20000);
  peerDuplex->txCredit(20000);
  {
    auto tx = peerDuplex->txStream_();
    tx << "rsp" << Zi::flush();
  }
  peerDuplex->fin();
  ZuCHECK(peerDuplex->txRangeCount() == 1 &&
      peerDuplex->txBufferedBytes() == 3 &&
      peerDuplex->txCreditAvailable() >= 3 &&
      peerDuplex->finSent(),
    "peer bidi response was not queued for transmit");
  unsigned queuedFlushes = duplexServer->txFlushQueued;
  ZuCHECK(duplexServer->flushCongestedStream(peerDuplex),
    "peer bidi response flush failed");
  ZuCHECK(peerDuplex->finDequeued(),
    "peer bidi response FIN was not dequeued");
  ZuCHECK(duplexServer->localStreamLimit(Zquic::StreamType::Duplex) == 2,
    "completed peer bidi stream did not return stream-count credit");
  ZuCHECK(duplexServer->queuedControlFrames() == queuedControls + 1,
    "completed peer bidi stream did not queue MAX_STREAMS");
  ZuCHECK(duplexServer->txFlushQueued == queuedFlushes + 1,
    "completed peer bidi stream did not request MAX_STREAMS flush");
  duplexServer->teardown();

  ZmRef<TestLink> txFirstServer = testLink(&app, true);
  txFirstServer->setLocalStreamLimit(Zquic::StreamType::Duplex, 1);
  packet = streamPkt(0, 0, "req", false, frame, used);
  ZuCHECK(packet && txFirstServer->receiveFrame(frame, packet) == 0,
    "Tx-first peer bidi stream receive setup failed");
  auto txFirst = txFirstServer->findStream(0);
  txFirstServer->initServerPath();
  txFirstServer->pathReceived(1200);
  txFirstServer->grantDataCredit(20000);
  txFirst->txCredit(20000);
  {
    auto tx = txFirst->txStream_();
    tx << "rsp" << Zi::flush();
  }
  txFirst->fin();
  ZuCHECK(txFirstServer->flushCongestedStream(txFirst) &&
      txFirst->finDequeued() &&
      txFirstServer->localStreamLimit(Zquic::StreamType::Duplex) == 1,
    "Tx-first peer bidi stream returned credit before Rx close");
  queuedControls = txFirstServer->queuedControlFrames();
  packet = streamPkt(0, 3, {}, true, frame, used);
  ZuCHECK(packet && txFirstServer->receiveFrame(frame, packet) == 0 &&
      txFirstServer->localStreamLimit(Zquic::StreamType::Duplex) == 2 &&
      txFirstServer->queuedControlFrames() == queuedControls + 1,
    "Tx-first peer bidi stream did not return one credit on Rx close");
  ZuCHECK(txFirstServer->receiveFrame(frame, packet) == 0 &&
      txFirstServer->localStreamLimit(Zquic::StreamType::Duplex) == 2 &&
      txFirstServer->queuedControlFrames() == queuedControls + 1,
    "duplicate peer FIN returned stream-count credit twice");
  txFirstServer->teardown();

  ZmRef<TestLink> resetServer = testLink(&app, true);
  resetServer->setLocalStreamLimit(Zquic::StreamType::Duplex, 1);
  packet = streamPkt(0, 0, "req", true, frame, used);
  ZuCHECK(packet && resetServer->receiveFrame(frame, packet) == 0,
    "peer bidi reset stream receive setup failed");
  auto peerReset = resetServer->findStream(0);
  ZuCHECK(peerReset &&
      resetServer->localStreamLimit(Zquic::StreamType::Duplex) == 1,
    "peer bidi reset stream returned credit before local reset");
  resetServer->initServerPath();
  resetServer->pathReceived(1200);
  peerReset->reset(1);
  ZuCHECK(resetServer->flushControlSends() && peerReset->resetSent() &&
      resetServer->localStreamLimit(Zquic::StreamType::Duplex) == 2,
    "completed peer bidi reset did not return stream-count credit");
  unsigned resetControls = resetServer->queuedControlFrames();
  peerReset->reset(1);
  ZuCHECK(resetServer->localStreamLimit(Zquic::StreamType::Duplex) == 2 &&
      resetServer->queuedControlFrames() == resetControls,
    "duplicate local reset returned stream-count credit twice");
  resetServer->teardown();
}

void testResetStopFrames()
{
  ZuTestScope(testResetStopFrames);

  App app;
  uint8_t b[128];
  Zquic::Frame frame;
  unsigned used = 0;

  ZmRef<TestLink> server = testLink(&app, true);
  int n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 0);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used),
    "RESET_STREAM setup failed");
  ZuCHECK(server->receiveFrame(frame) == 0, "server rejected peer RESET_STREAM");
  auto reset = server->findStream(0);
  ZuCHECK(reset &&
      server->streamedCount == 1 &&
      reset->resetReceived() &&
      reset->rxError() == Zquic::StreamError::Reset &&
      reset->rxAppError() == 7 &&
      reset->rxComplete() &&
      !reset->finalSize(),
    "RESET_STREAM state mismatch");

  ZmRef<TestLink> client = testLink(&app);
  auto local = client->stream(Zquic::StreamType::Duplex);
  n = Zquic::FrameCodec::writeStopSending(b, sizeof(b), local->id(), 9);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used),
    "STOP_SENDING setup failed");
  ZuCHECK(client->receiveFrame(frame) == 0 &&
      local->stopReceived() &&
      local->txError() == Zquic::StreamError::Stop &&
      local->txAppError() == 9,
    "STOP_SENDING state mismatch");

  auto delivered = client->stream(Zquic::StreamType::Duplex);
  auto packet = streamPkt(delivered->id(), 0, "hello", false, frame, used);
  ZuCHECK(packet &&
      delivered->receiveFrame(frame, packet),
    "delivered STREAM setup failed");
  n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), delivered->id(), 1, 3);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used),
    "final-size violating RESET_STREAM setup failed");
  ZuCHECK(!delivered->receiveReset(frame),
    "RESET_STREAM final-size violation was accepted");
}

void testLocalResetStopSend()
{
  ZuTestScope(testLocalResetStopSend);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  Zquic::SentFrameRef refs[2];

  auto reset = link->stream(Zquic::StreamType::Duplex);
  {
    auto tx = reset->txStream_();
    tx << "abc" << Zi::flush();
  }
  reset->reset(42);
  ZuCHECK(reset->resetSent() &&
      reset->txError() == Zquic::StreamError::Reset &&
      reset->txAppError() == 42 &&
      !reset->txRangeCount() && !reset->txBufferedBytes() && !reset->txQueueCount() &&
      link->queuedControlFrames() == 1,
    "local RESET_STREAM did not queue control state");
  unsigned n = link->flushSentRefs(refs, 2);
  ZuCHECK(n == 1 &&
      refs[0].kind == Zquic::SentFrameKind::Control &&
      refs[0].controlType == Zquic::FrameType::ResetStream &&
      refs[0].streamID == uint64_t(reset->id()) &&
      refs[0].value == 42 &&
      !refs[0].length &&
      !link->queuedControlFrames(),
    "unsent application bytes entered RESET_STREAM final size");
  Zquic::Frame rebuilt;
  ZuCHECK(link->rebuildControl(refs[0], rebuilt) &&
      rebuilt.type == Zquic::FrameType::ResetStream &&
      rebuilt.streamID == uint64_t(reset->id()) &&
      rebuilt.errorCode == 42 &&
      !rebuilt.length,
    "local RESET_STREAM retransmit rebuild failed");
  ZuCHECK(!reset->ackReset(42, 3) && reset->ackReset(42, 0),
    "RESET_STREAM acknowledgement used queued application extent");

  auto partial = link->stream(Zquic::StreamType::Duplex);
  partial->txCredit(2);
  link->grantDataCredit(6);
  {
    auto tx = partial->txStream_();
    tx << "abcdef" << Zi::flush();
  }
  n = link->flushSentRefs(refs, 2);
  ZuCHECK(n == 1 && refs[0].kind == Zquic::SentFrameKind::Stream &&
      refs[0].length == 2 && partial->txBufferedBytes() == 4,
    "partial STREAM transmission did not stop at peer credit");
  ZuCHECK(partial->recordTxUnackd(0, 2, false),
    "partial STREAM recovery range setup failed");
  partial->reset(43);
  ZuCHECK(!partial->txRangeCount() && !partial->txBufferedBytes() &&
      !partial->txQueueCount() && !partial->txUnackdCount(),
    "RESET_STREAM retained unsent or recovery ranges");
  n = link->flushSentRefs(refs, 2);
  ZuCHECK(n == 1 && refs[0].controlType == Zquic::FrameType::ResetStream &&
      refs[0].streamID == uint64_t(partial->id()) && refs[0].length == 2 &&
      link->rebuildControl(refs[0], rebuilt) && rebuilt.length == 2 &&
      !partial->ackReset(43, 6) && partial->ackReset(43, 2),
    "flow-stalled RESET_STREAM included unsent bytes in final size");

  auto stop = link->stream(Zquic::StreamType::Duplex);
  stop->stop(9);
  ZuCHECK(stop->stopSent() &&
      stop->rxError() == Zquic::StreamError::Stop &&
      stop->rxAppError() == 9 &&
      link->queuedControlFrames() == 1,
    "local STOP_SENDING did not queue control state");
  n = link->flushSentRefs(refs, 2);
  ZuCHECK(n == 1 &&
      refs[0].kind == Zquic::SentFrameKind::Control &&
      refs[0].controlType == Zquic::FrameType::StopSending &&
      refs[0].streamID == uint64_t(stop->id()) &&
      refs[0].value == 9,
    "local STOP_SENDING sent ref mismatch");
  ZuCHECK(link->rebuildControl(refs[0], rebuilt) &&
      rebuilt.type == Zquic::FrameType::StopSending &&
      rebuilt.streamID == uint64_t(stop->id()) &&
      rebuilt.errorCode == 9,
    "local STOP_SENDING retransmit rebuild failed");

  link->sendControlRef(1, refs[0]);
  link->sendControlRef(2, refs[0]);
  link->sendAckEliciting(5);
  link->ackOnly(1);
  link->ackOnly(5);
  Zquic::SentFrameRef ref;
  Zquic::PktNumSpace::T level = Zquic::PktNumSpace::Initial;
  ZuCHECK(!link->nextRetransmitRef(level, ref),
    "ACKd STOP_SENDING was retransmitted after later loss");
  link->teardown();

  ZmRef<TestLink> lost = testLink(&app);
  auto lostStop = lost->stream(Zquic::StreamType::Duplex);
  lostStop->stop(11);
  n = lost->flushSentRefs(refs, 2);
  ZuCHECK(n == 1 &&
      refs[0].kind == Zquic::SentFrameKind::Control &&
      refs[0].controlType == Zquic::FrameType::StopSending,
    "lost STOP_SENDING sent ref setup failed");
  lost->sendControlRef(2, refs[0]);
  lost->sendAckEliciting(5);
  lost->ackOnly(5);
  ZuCHECK(lost->nextRetransmitRef(level, ref) &&
      ref.kind == Zquic::SentFrameKind::Control &&
      ref.controlType == Zquic::FrameType::StopSending &&
      ref.streamID == uint64_t(lostStop->id()) &&
      !lost->nextRetransmitRef(level, ref),
    "lost STOP_SENDING did not queue one retransmit");
  lost->teardown();
}

void testMaxBlockedValidate()
{
  ZuTestScope(testMaxBlockedValidate);

  App app;
  uint8_t b[128];
  Zquic::Frame frame;

  ZmRef<TestLink> client = testLink(&app);
  auto localDuplex = client->stream(Zquic::StreamType::Duplex);
  auto localUni = client->stream(Zquic::StreamType::Simplex);
  int n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), localDuplex->id(), 4096);
  ZuCHECK(parseFrame(b, n, frame) && client->applyMaxStreamData(frame) &&
      localDuplex->txCreditLimit() == 4096,
    "MAX_STREAM_DATA did not extend local bidirectional stream credit");

  n = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 8, 4096);
#ifdef Zquic_DEBUG
  uint64_t invalidMax = client->diag().rx.streamMaxInvalidRx;
#endif
  ZuCHECK(parseFrame(b, n, frame) && client->applyMaxStreamData(frame) &&
      !client->findStream(8),
    "MAX_STREAM_DATA for uninitiated local stream was not diagnosed");
#ifdef Zquic_DEBUG
  ZuCHECK(client->diag().rx.streamMaxInvalidRx == invalidMax + 1,
    "uninitiated local MAX_STREAM_DATA diagnostics mismatch");
#endif

  n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), localUni->id(), 4096);
  ZuCHECK(parseFrame(b, n, frame) && client->applyMaxStreamData(frame),
    "MAX_STREAM_DATA for local unidirectional sender was rejected");

  n = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 3, 4096);
#ifdef Zquic_DEBUG
  invalidMax = client->diag().rx.streamMaxInvalidRx;
#endif
  ZuCHECK(parseFrame(b, n, frame) && client->applyMaxStreamData(frame) &&
      !client->findStream(3),
    "MAX_STREAM_DATA for peer unidirectional stream was not diagnosed");
#ifdef Zquic_DEBUG
  ZuCHECK(client->diag().rx.streamMaxInvalidRx == invalidMax + 1,
    "peer unidirectional MAX_STREAM_DATA diagnostics mismatch");
#endif

  ZmRef<TestLink> server = testLink(&app, true);
  server->setLocalStreamLimit(Zquic::StreamType::Duplex, 2);
  n = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 0, 2048);
  ZuCHECK(parseFrame(b, n, frame) && server->applyMaxStreamData(frame) &&
      server->findStream(0) &&
      server->peerStreamsOpened(Zquic::StreamType::Duplex) == 1,
    "MAX_STREAM_DATA did not create valid peer bidirectional stream");

  ZmRef<TestLink> limited = testLink(&app, true);
  limited->setLocalStreamLimit(Zquic::StreamType::Duplex, 1);
  n = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 4, 2048);
#ifdef Zquic_DEBUG
  invalidMax = limited->diag().rx.streamMaxInvalidRx;
#endif
  ZuCHECK(parseFrame(b, n, frame) && limited->applyMaxStreamData(frame) &&
      !limited->findStream(4),
    "MAX_STREAM_DATA beyond local stream limit was not diagnosed");
#ifdef Zquic_DEBUG
  ZuCHECK(limited->diag().rx.streamMaxInvalidRx == invalidMax + 1,
    "over-limit MAX_STREAM_DATA diagnostics mismatch");
#endif

  n = Zquic::FrameCodec::writeDataBlocked(
    b, sizeof(b), server->rxDataCreditLimit());
  ZuCHECK(parseFrame(b, n, frame) && server->receiveDataBlocked(frame),
    "DATA_BLOCKED at local receive limit was rejected");
  n = Zquic::FrameCodec::writeDataBlocked(
    b, sizeof(b), server->rxDataCreditLimit() + 1);
  ZuCHECK(parseFrame(b, n, frame) && !server->receiveDataBlocked(frame),
    "DATA_BLOCKED above local receive limit was accepted");

  n = Zquic::FrameCodec::writeStreamsBlocked(
    b, sizeof(b), Zquic::StreamType::Duplex,
    server->localStreamLimit(Zquic::StreamType::Duplex));
  ZuCHECK(parseFrame(b, n, frame) && server->receiveStreamsBlocked(frame),
    "STREAMS_BLOCKED at local advertised peer limit was rejected");
  n = Zquic::FrameCodec::writeStreamsBlocked(
    b, sizeof(b), Zquic::StreamType::Duplex,
    server->localStreamLimit(Zquic::StreamType::Duplex) + 1);
  ZuCHECK(parseFrame(b, n, frame) && !server->receiveStreamsBlocked(frame),
    "STREAMS_BLOCKED above local advertised peer limit was accepted");
}

void testStreamBlockedValidate()
{
  ZuTestScope(testStreamBlockedValidate);

  App app;
  uint8_t b[128];
  Zquic::Frame frame;

  ZmRef<TestLink> server = testLink(&app, true);
  server->setLocalStreamLimit(Zquic::StreamType::Duplex, 2);
  int n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 0, 128);
  ZuCHECK(parseFrame(b, n, frame) &&
      server->rxStreamBlocked(frame) &&
      server->findStream(0) &&
      server->findStream(0)->rxCreditUsed() == 0 &&
      server->rxDataCreditUsed() == 0,
    "valid STREAM_DATA_BLOCKED mutated receive credit");

  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 0, 64);
  ZuCHECK(parseFrame(b, n, frame) &&
      server->rxStreamBlocked(frame) &&
      server->findStream(0)->rxCreditUsed() == 0 &&
      server->rxDataCreditUsed() == 0,
    "decreasing STREAM_DATA_BLOCKED mutated receive credit");

  n = Zquic::FrameCodec::writeStreamDataBlocked(
    b, sizeof(b), 0, server->findStream(0)->rxCreditLimit() + 1);
  ZuCHECK(parseFrame(b, n, frame) &&
      !server->rxStreamBlocked(frame),
    "STREAM_DATA_BLOCKED beyond stream receive limit was accepted");

  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 4, 128);
  ZuCHECK(parseFrame(b, n, frame) &&
      server->rxStreamBlocked(frame) &&
      server->findStream(4) &&
      server->peerStreamsOpened(Zquic::StreamType::Duplex) == 2,
    "valid unopened peer STREAM_DATA_BLOCKED did not create stream");

  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 8, 128);
  ZuCHECK(parseFrame(b, n, frame) &&
      !server->rxStreamBlocked(frame) &&
      !server->findStream(8),
    "STREAM_DATA_BLOCKED opened stream beyond stream-count limit");

  ZmRef<TestLink> client = testLink(&app);
  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 2, 128);
  ZuCHECK(parseFrame(b, n, frame) &&
      !client->rxStreamBlocked(frame),
    "STREAM_DATA_BLOCKED for local unidirectional stream was accepted");

  ZmRef<TestLink> small = testLink(&app, true);
  small->setLocalStreamLimit(Zquic::StreamType::Duplex, 1);
  n = Zquic::FrameCodec::writeStreamDataBlocked(
    b, sizeof(b), 0, small->rxDataCreditLimit() + 1);
  ZuCHECK(parseFrame(b, n, frame) &&
      !small->rxStreamBlocked(frame),
    "STREAM_DATA_BLOCKED beyond connection receive limit was accepted");
}

void invalidClosedStreamActivity()
{
  ZuTestScopeRT(invalidClosedStreamActivity);

  App app;
  uint8_t b[128];
  Zquic::Frame frame;
  unsigned used = 0;

  ZmRef<TestLink> maxLink = testLink(&app);
  auto local = maxLink->stream(Zquic::StreamType::Duplex);
  local->reset(1);
  int n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), uint64_t(local->id()), 4096);
  ZQUIC_CHECK_RT(parseFrame(b, n, frame) &&
      maxLink->applyMaxStreamData(frame) &&
      !maxLink->closeError(),
    "closed-stream MAX_STREAM_DATA was not ignored");
#ifdef Zquic_DEBUG
  Zquic::LinkDiag diag = maxLink->diag();
  ZQUIC_CHECK_RT(!diag.rx.invalidStreamFrames &&
      !diag.rx.closedStreamFrames &&
      diag.rx.streamMaxClosedRx == 1 &&
      !diag.rx.suspectStreamCloses,
    "closed-stream MAX_STREAM_DATA diagnostics mismatch");
#endif

  ZmRef<TestLink> resetLink = testLink(&app, true);
  n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 0);
  ZQUIC_CHECK_RT(parseFrame(b, n, frame) &&
      resetLink->receiveFrame(frame) == 0 &&
      resetLink->receiveFrame(frame) == 0,
    "duplicate RESET_STREAM final size was not ignored");
#ifdef Zquic_DEBUG
  diag = resetLink->diag();
  ZQUIC_CHECK_RT(!diag.rx.invalidStreamFrames &&
      !diag.rx.closedStreamFrames &&
      !resetLink->closeError(),
    "duplicate RESET_STREAM diagnostics mismatch");
#endif
  n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 1);
  ZQUIC_CHECK_RT(parseFrame(b, n, frame) &&
      resetLink->receiveFrame(frame) < 0 &&
      resetLink->closeError() == Zquic::TransportError::FinalSize,
    "active duplicate RESET_STREAM final-size violation did not close");
#ifdef Zquic_DEBUG
  diag = resetLink->diag();
  ZQUIC_CHECK_RT(diag.rx.suspectStreamCloses == 1,
    "final-size violation close diagnostics mismatch");
#endif

  ZmRef<TestLink> blockedLink = testLink(&app, true);
  n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 0);
  ZQUIC_CHECK_RT(parseFrame(b, n, frame) &&
      blockedLink->receiveFrame(frame) == 0,
    "closed STREAM_DATA_BLOCKED reset setup failed");
  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 0, 0);
  ZQUIC_CHECK_RT(parseFrame(b, n, frame) &&
      blockedLink->rxStreamBlocked(frame) &&
      !blockedLink->closeError(),
    "closed-stream STREAM_DATA_BLOCKED was not ignored");
#ifdef Zquic_DEBUG
  diag = blockedLink->diag();
  ZQUIC_CHECK_RT(diag.rx.invalidStreamFrames == 1 &&
      diag.rx.closedStreamFrames == 1,
    "closed-stream STREAM_DATA_BLOCKED diagnostics mismatch");
#endif

  ZmRef<TestLink> dupLink = testLink(&app);
  auto dup = dupLink->stream(Zquic::StreamType::Duplex);
  auto packet = streamPkt(dup->id(), 0, "dup", false, frame, used);
  ZQUIC_CHECK_RT(packet &&
      dupLink->receiveFrame(frame, packet) == 0,
    "duplicate STREAM setup failed");
  packet = streamPkt(dup->id(), 0, "dup", false, frame, used);
  ZQUIC_CHECK_RT(packet &&
      dupLink->receiveFrame(frame, packet) == 0 &&
      !dupLink->diag().rx.invalidStreamFrames,
    "ordinary duplicate STREAM was treated as invalid");

  ZmRef<TestLink> threshold = testLink(&app);
  auto noisy = threshold->stream(Zquic::StreamType::Duplex);
  noisy->reset(3);
  n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), uint64_t(noisy->id()), 4096);
  ZQUIC_CHECK_RT(parseFrame(b, n, frame), "threshold frame setup failed");
  for (unsigned i = 0, n = TestLink::suspiciousStreamThreshold(); i < n; ++i)
    ZQUIC_CHECK_RT(threshold->applyMaxStreamData(frame),
      "closed-stream threshold frame was rejected");
#ifdef Zquic_DEBUG
  diag = threshold->diag();
  ZQUIC_CHECK_RT(!threshold->closeError() &&
      diag.rx.streamMaxClosedRx == TestLink::suspiciousStreamThreshold() &&
      !diag.rx.suspectStreamCloses,
    "closed-stream MAX_STREAM_DATA was not tracked as benign");
  ZQUIC_CHECK_RT(threshold->applyMaxStreamData(frame) &&
      threshold->diag().rx.streamMaxClosedRx ==
	TestLink::suspiciousStreamThreshold() + 1 &&
      !threshold->diag().rx.suspectStreamCloses,
    "closed-stream MAX_STREAM_DATA produced suspicious close diagnostics");
#else
  ZQUIC_CHECK_RT(!threshold->closeError() &&
      threshold->applyMaxStreamData(frame),
    "closed-stream MAX_STREAM_DATA was not handled as benign");
#endif
}

void testStreamGC()
{
  ZuTestScope(testStreamGC);

  App app;

  ZmRef<TestLink> held = testLink(&app);
  auto live = held->stream(Zquic::StreamType::Simplex);
  uint64_t liveID = uint64_t(live->id());
  live->fin();
  held->scheduleStream(live);
  Zquic::SentFrameRef refs[4];
  ZuCHECK(held->flushRecordedRefs(refs, 4) == 1 && live->finDequeued() &&
      live->txUnackdCount(),
    "held stream terminal send setup failed");
  ZuCHECK(held->findStream(int64_t(liveID)),
    "stream was reaped before terminal ACK");
  held->ackOnly(0);
  ZuCHECK(!held->findStream(int64_t(liveID)) &&
      live && uint64_t(live->id()) == liveID,
    "terminal ACK did not remove stream from active index");
  held->teardown();

  ZmRef<TestLink> maxLink = testLink(&app);
  auto local = maxLink->stream(Zquic::StreamType::Simplex);
  uint64_t localID = uint64_t(local->id());
  local->fin();
  maxLink->scheduleStream(local);
  ZuCHECK(maxLink->flushRecordedRefs(refs, 4) == 1 && local->finDequeued() &&
      local->txUnackdCount(),
    "closed MAX_STREAM_DATA setup failed");
  local = nullptr;
  maxLink->ackOnly(0);
  ZuCHECK(!maxLink->findStream(int64_t(localID)),
    "closed MAX_STREAM_DATA stream was not reaped");
  maxLink->teardown();
  uint8_t b[128];
  Zquic::Frame frame;
  int n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), localID, 4096);
  ZuCHECK(parseFrame(b, n, frame) &&
      maxLink->applyMaxStreamData(frame),
    "closed MAX_STREAM_DATA after stream GC was not compact-handled");
#ifdef Zquic_DEBUG
  ZuCHECK(maxLink->diag().rx.streamMaxClosedRx == 1,
    "closed MAX_STREAM_DATA after stream GC diagnostics mismatch");
#endif

  ZmRef<TestLink> ackLink = testLink(&app);
  auto ackd = ackLink->stream(Zquic::StreamType::Simplex);
  uint64_t ackdID = uint64_t(ackd->id());
  ackd->fin();
  ackLink->scheduleStream(ackd);
  ZuCHECK(ackLink->flushRecordedRefs(refs, 4) == 1 && ackd->finDequeued() &&
      ackd->txUnackdCount(),
    "ACK-driven stream GC setup failed");
  ackd = nullptr;
  ZuCHECK(ackLink->findStream(int64_t(ackdID)),
    "stream was reaped before FIN ACK");
  ackLink->ackOnly(0);
  ZuCHECK(!ackLink->findStream(int64_t(ackdID)),
    "ACK processing did not reap terminal local stream");
  ackLink->teardown();

  ZmRef<TestLink> rxLink = testLink(&app, true);
  unsigned used = 0;
  auto packet = streamPkt(2, 0, "", true, frame, used);
  ZuCHECK(packet && rxLink->receiveFrame(frame, packet) == 0,
    "peer unidirectional stream GC setup failed");
  ZmRef<TestStream> rx = rxLink->lastStream;
  ZuCHECK(rx && rx->rxComplete(),
    "peer unidirectional stream did not reach terminal receive state");
  ZuCHECK(!rxLink->findStream(2),
    "terminal FIN did not remove peer stream from active index");
  rxLink->lastStream = nullptr;
  rx = nullptr;
  packet = streamPkt(2, 0, "", true, frame, used);
  ZuCHECK(packet && rxLink->receiveFrame(frame, packet) == 0 &&
      !rxLink->diag().rx.closedStreamFrames,
    "duplicate STREAM after stream GC was not compact-handled");
  packet = streamPkt(2, 0, "long", true, frame, used);
  ZuCHECK(packet && rxLink->receiveFrame(frame, packet) == 0 &&
      !rxLink->closeError(),
    "late STREAM after stream GC was not classified as closed");
}

void frameRoleAndSpaceLegality()
{
  ZuTestScope(frameRoleAndSpaceLegality);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  ZmRef<TestLink> server = testLink(&app, true);
  Zquic::Frame frame;

  frame.type = Zquic::FrameType::Ping;
  ZuCHECK(client->frameLegal(Zquic::PktNumSpace::Initial, frame) &&
      client->frameLegal(Zquic::PktNumSpace::Handshake, frame) &&
      client->frameLegal(Zquic::PktNumSpace::AppData, frame),
    "PING frame-space legality mismatch");

  frame.reset();
  frame.type = Zquic::FrameType::Stream;
  ZuCHECK(!client->frameLegal(Zquic::PktNumSpace::Initial, frame) &&
      !client->frameLegal(Zquic::PktNumSpace::Handshake, frame) &&
      client->frameLegal(Zquic::PktNumSpace::AppData, frame),
    "STREAM frame-space legality mismatch");

  frame.reset();
  frame.type = Zquic::FrameType::Crypto;
  ZuCHECK(client->frameLegal(Zquic::PktNumSpace::Initial, frame) &&
      client->frameLegal(Zquic::PktNumSpace::Handshake, frame) &&
      !client->frameLegal(Zquic::PktNumSpace::AppData, frame),
    "CRYPTO frame-space legality mismatch");

  frame.reset();
  frame.type = Zquic::FrameType::Ack;
  frame.ackRanges.push(Zquic::AckRange{0, 0});
  ZuCHECK(client->frameLegal(Zquic::PktNumSpace::Initial, frame),
    "ACK frame-space legality mismatch");
  ZuCHECK(!client->ackValid(Zquic::PktNumSpace::Initial, 0, 0),
    "ACK for unsent packet number was accepted by Tx validation");

  frame.reset();
  frame.type = Zquic::FrameType::NewToken;
  frame.payload = ZuBSpan{"token"};
  ZuCHECK(client->frameLegal(Zquic::PktNumSpace::AppData, frame) &&
      !server->frameLegal(Zquic::PktNumSpace::AppData, frame) &&
      !client->frameLegal(Zquic::PktNumSpace::Handshake, frame),
    "NEW_TOKEN role/space legality mismatch");
  frame.payload = {};
  ZuCHECK(!client->frameLegal(Zquic::PktNumSpace::AppData, frame),
    "empty NEW_TOKEN was accepted");

  frame.reset();
  frame.type = Zquic::FrameType::HandshakeDone;
  ZuCHECK(client->frameLegal(Zquic::PktNumSpace::AppData, frame) &&
      !server->frameLegal(Zquic::PktNumSpace::AppData, frame) &&
      !client->frameLegal(Zquic::PktNumSpace::Handshake, frame),
    "HANDSHAKE_DONE role/space legality mismatch");

  frame.reset();
  frame.type = Zquic::FrameType::ApplicationClose;
  ZuCHECK(!client->frameLegal(Zquic::PktNumSpace::Initial, frame) &&
      client->frameLegal(Zquic::PktNumSpace::AppData, frame),
    "APPLICATION_CLOSE frame-space legality mismatch");

  frame.reset();
  frame.type = Zquic::FrameType::Ping;
  ZuCHECK(client->frameLegal(Zquic::PktNumSpace::AppData, frame, true) &&
      !client->frameLegal(Zquic::PktNumSpace::Initial, frame, true),
    "0-RTT PING legality mismatch");

  frame.reset();
  frame.type = Zquic::FrameType::Stream;
  ZuCHECK(client->frameLegal(Zquic::PktNumSpace::AppData, frame, true),
    "0-RTT STREAM was rejected");

  frame.reset();
  frame.type = Zquic::FrameType::Ack;
  ZuCHECK(!client->frameLegal(Zquic::PktNumSpace::AppData, frame, true),
    "0-RTT ACK was accepted");

  frame.reset();
  frame.type = Zquic::FrameType::Crypto;
  ZuCHECK(!client->frameLegal(Zquic::PktNumSpace::AppData, frame, true),
    "0-RTT CRYPTO was accepted");

  frame.reset();
  frame.type = Zquic::FrameType::ResetStream;
  ZuCHECK(!client->frameLegal(Zquic::PktNumSpace::AppData, frame, true),
    "0-RTT RESET_STREAM was accepted before cleanup support");

  frame.reset();
  frame.type = Zquic::FrameType::HandshakeDone;
  ZuCHECK(!client->frameLegal(Zquic::PktNumSpace::AppData, frame, true),
    "0-RTT HANDSHAKE_DONE was accepted");

  frame.reset();
  frame.type = Zquic::FrameType::Unknown;
  ZuCHECK(!client->frameLegal(Zquic::PktNumSpace::AppData, frame),
    "unknown extension frame was accepted");
}

void testZeroRTTProtectedSend()
{
  ZuTestScope(testZeroRTTProtectedSend);

  uint8_t secretBytes[32];
  for (unsigned i = 0; i < sizeof(secretBytes); ++i)
    secretBytes[i] = uint8_t(0x80 + i);
  Zquic::TrafficSecret secret;
  ZuCHECK(Zquic::PktProt::deriveSecret(
      secret, &ptls_openssl_aes128gcmsha256,
      span_(secretBytes, sizeof(secretBytes))),
    "0-RTT traffic secret derivation failed");

  App app;
  ZmRef<TestLink> link = testLink(&app);
  ZmRef<ZiIOBuf> sent;
  ZuCHECK(!link->sendZeroRTTProbe(sent) && !sent,
    "0-RTT send succeeded without early keys");
  ZuCHECK(link->installZeroRTTTx(secret),
    "0-RTT Tx key install failed");
  ZuCHECK(link->sendZeroRTTProbe(sent) && sent && sent->length,
    "0-RTT protected send failed");

  Zquic::LongHdr h;
  ZuCHECK(Zquic::Pkt::parseLong(
      ZuBSpan{sent->data(), sent->length}, h) > 0 &&
      h.type == Zquic::PktType::ZeroRTT,
    "0-RTT packet header was not written");

  Zquic::PktProtState rx;
  ZuCHECK(rx.init(secret, Zquic::PktNumSpace::AppData, false),
    "0-RTT Rx protection state init failed");
  uint64_t pn = 0;
  unsigned payloadOffset = 0;
  int plainLen = Zquic::PktProt::unprotectLong(
    sent->data_(), sent->length, rx, 0, h.pnOffset, pn, payloadOffset);
  ZuCHECK(plainLen >= 1 && pn == 0 && payloadOffset < sent->length &&
      sent->data_()[payloadOffset] == uint8_t(Zquic::FrameType::Ping),
    "0-RTT protected packet did not decrypt to PING");
#ifdef Zquic_DEBUG
  ZuCHECK(link->diag().tx.packetsTx == 1,
    "0-RTT send did not record AppData packet accounting");
#endif
  link->teardown();
}

void testZeroRTTEarlyStreamPolicy()
{
  ZuTestScope(testZeroRTTEarlyStreamPolicy);

  uint8_t secretBytes[32];
  for (unsigned i = 0; i < sizeof(secretBytes); ++i)
    secretBytes[i] = uint8_t(0xa0 + i);
  Zquic::TrafficSecret secret;
  ZuCHECK(Zquic::PktProt::deriveSecret(
      secret, &ptls_openssl_aes128gcmsha256,
      span_(secretBytes, sizeof(secretBytes))),
    "0-RTT traffic secret derivation failed");

  App app;
  ZmRef<TestLink> link = testLink(&app);
  ZuCHECK(link->installZeroRTTTx(secret),
    "0-RTT Tx key install failed");
  link->grantDataCredit(20000);
  auto stream = link->stream(Zquic::StreamType::Duplex);
  stream->txCredit(20000);
  {
    auto tx = stream->txStream_();
    tx << "early" << Zi::flush();
  }
  link->scheduleStream(stream);
  ZuCHECK(link->queueMaxData(1234) && link->queuedControlFrames() == 1,
    "0-RTT stream policy control setup failed");

  Zquic::SentFrameRef refs[4];
  ZuCHECK(!link->flushEarlyStreamRefs(refs, 4) &&
      stream->txRangeCount() == 1 &&
      link->scheduledStreams() == 1 &&
      app.earlyStreamChecks == 1,
    "default 0-RTT stream denial consumed or unscheduled data");

  app.earlyStreamAllowed = true;
  unsigned n = link->flushEarlyStreamRefs(refs, 4);
  ZuCHECK(n == 1 &&
      refs[0].kind == Zquic::SentFrameKind::Stream &&
      refs[0].streamID == uint64_t(stream->id()) &&
      refs[0].length == 5 &&
      !refs[0].fin &&
      !stream->txRangeCount() &&
      link->queuedControlFrames() == 1,
    "0-RTT stream flush did not send only eligible STREAM data");
  ZuCHECK(app.lastEarlyStreamID == uint64_t(stream->id()) &&
      !app.lastEarlyStreamFin,
    "0-RTT stream policy hook saw wrong stream metadata");

  Zquic::PktNumSpace::T level = Zquic::PktNumSpace::N;
  Zquic::SentFrameRef retx;
  ZuCHECK(link->canTxZeroRTT(), "0-RTT Tx key missing before rejection");
  ZuCHECK(link->rejectZeroRTT() == 1 &&
      !link->canTxZeroRTT() &&
      link->retransmits == 1 &&
      link->nextRetransmitRef(level, retx) &&
      level == Zquic::PktNumSpace::AppData &&
      retx.kind == Zquic::SentFrameKind::Stream &&
      retx.streamID == uint64_t(stream->id()) &&
      retx.offset == 0 &&
      retx.length == 5 &&
      !retx.fin,
    "0-RTT rejection did not retire packet and preserve stream retransmit");
  link->teardown();
}

#ifdef Zquic_DEBUG
void testZeroRTTAcceptQLog()
{
  ZuTestScope(testZeroRTTAcceptQLog);

  Zi::Path path = testPath("ZquicStreamZeroRTTAcceptQLog.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).ringSize(1<<15);

  App app;
  ZuCHECK(ZquicLogger::init(
      app.qlogTrace(), params, Zquic::Vantage::Unknown),
    "0-RTT accept qlog init failed");
  ZquicLogger::start();

  Zquic::TrafficSecret secret;
  ZuCHECK(trafficSecret(secret, 0xd0),
    "0-RTT accept traffic secret derivation failed");

  ZmRef<TestLink> client = testLink(&app);
  ZmRef<TestLink> server = testLink(&app, true);
  ZuCHECK(client->installZeroRTTTx(secret),
    "0-RTT accept client Tx key install failed");
  ZuCHECK(server->installZeroRTTRx(secret),
    "0-RTT accept server Rx key install failed");

  ZmRef<ZiIOBuf> sent;
  ZuCHECK(client->sendZeroRTTProbe(sent) && sent && sent->length,
    "0-RTT accept probe send failed");
  ZuCHECK(client->txEarlyState() == Zquic::LinkEarlyState::Offered,
    "0-RTT accept client did not enter offered state");
  ZuCHECK(server->receiveZeroRTT(ZuMv(sent)) &&
      server->rxEarlyState() == Zquic::LinkEarlyState::Recv,
    "0-RTT accept server did not record received early data");

  ZuCHECK(server->acceptZeroRTTRx() &&
      server->rxEarlyState() == Zquic::LinkEarlyState::Accepted,
    "0-RTT accept server Rx transition failed");
  ZuCHECK(client->acceptZeroRTTTx() &&
      client->txEarlyState() == Zquic::LinkEarlyState::Accepted,
    "0-RTT accept client Tx transition failed");

  closeQLog(app.qlogTrace());
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(app.qlogTrace());
  client->teardown();
  server->teardown();

  ZuCHECK(diag.recordsEnqueued >= 3, "0-RTT accept qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 4, "0-RTT accept qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "0-RTT accept qlog writer failure");

  ZtString<> qlog = readFile_(path);
  ZuCHECK(qlog, "0-RTT accept qlog output missing");
  ZuCHECK(parseJSONSeq(qlog) >= 4,
    "0-RTT accept qlog JSON-SEQ parse failed");
  ZuCHECK(qlog.find<"zquic:zero_rtt_accepted">() >= 0 &&
      qlog.find<"\"reason\":\"0rtt\"">() >= 0 &&
      qlog.find<"\"success\":true">() >= 0,
    "0-RTT accept qlog event missing");
}

void zeroRTTAfterOneRTTRejected()
{
  ZuTestScope(zeroRTTAfterOneRTTRejected);

  Zi::Path path = testPath("ZquicStreamZeroRTTRejectQLog.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).ringSize(1<<15);

  App app;
  ZuCHECK(ZquicLogger::init(
      app.qlogTrace(), params, Zquic::Vantage::Unknown),
    "0-RTT reject qlog init failed");
  ZquicLogger::start();

  Zquic::TrafficSecret rx;
  Zquic::TrafficSecret tx;
  ZuCHECK(trafficSecret(rx, 0xb0) && trafficSecret(tx, 0xc0),
    "1-RTT traffic secret derivation failed");

  ZmRef<TestLink> link = testLink(&app);
  Zquic::CxnID dcid{"server01"};
  ZuCHECK(link->installOneRTT(rx, tx, dcid),
    "1-RTT key install failed");
  ZuCHECK(link->receiveShort(shortPing(dcid, rx, 0, false)) &&
      link->rxOneRTTSeen(),
    "1-RTT receive did not advance AppData receive state");
  uint64_t packets = link->diag().rx.packetsRx;

  ZuCHECK(link->receiveZeroRTT(zeroRTTLongHdr(1)),
    "late 0-RTT packet was treated as a receive failure");
  ZuCHECK(link->rxEarlyState() == Zquic::LinkEarlyState::Rejected &&
      link->txEarlyState() == Zquic::LinkEarlyState::Rejected &&
      link->diag().rx.packetsRx == packets,
    "late 0-RTT was not rejected before decrypt/accounting");

  closeQLog(app.qlogTrace());
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(app.qlogTrace());
  link->teardown();

  ZuCHECK(diag.recordsEnqueued >= 3, "0-RTT reject qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 4, "0-RTT reject qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "0-RTT reject qlog writer failure");

  ZtString<> qlog = readFile_(path);
  ZuCHECK(qlog, "0-RTT reject qlog output missing");
  ZuCHECK(parseJSONSeq(qlog) >= 4,
    "0-RTT reject qlog JSON-SEQ parse failed");
  ZuCHECK(qlog.find<"zquic:zero_rtt_rejected">() >= 0 &&
      qlog.find<"\"reason\":\"0rtt_after_1rtt\"">() >= 0 &&
      qlog.find<"\"success\":false">() >= 0,
    "0-RTT reject qlog event missing");
  ZuCHECK(qlog.find<"quic:packet_dropped">() >= 0 &&
      qlog.find<"0rtt_after_1rtt">() >= 0,
    "late 0-RTT packet drop qlog event missing");
}

void testRxQLog()
{
  ZuTestScope(testRxQLog);

  Zi::Path path = testPath("ZquicStreamRuntimeQLog.sqlog");
  ZiFile::remove(path);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  Zquic::CxnID cid{"qlogcid1"};
  Zquic::TrafficSecret secret;
  ZuCHECK(trafficSecret(secret, 11), "1-RTT secret derivation failed");
  ZuCHECK(link->installOneRTT(secret, secret, cid),
    "test 1-RTT secret install failed");

  bool disabledSeen = true;
  ZuCHECK(link->receiveShort(shortPing(cid, secret, 1, false), &disabledSeen),
    "disabled-qlog receive packet was rejected");
  ZuCHECK(!disabledSeen, "disabled qlog constructed receive accumulator");

	ZquicLogParams params;
	params.enabled(true).path(path).ringSize(1<<15);
	ZuCHECK(ZquicLogger::init(
	    app.qlogTrace(), params, Zquic::Vantage::Unknown),
	  "runtime qlog init failed");
	ZquicLogger::start();
  ZuCHECK(ZquicLogger::enabled(), "runtime qlog did not enable");

  bool enabledSeen = false;
  ZuCHECK(link->receiveShort(shortPing(cid, secret, 2, false), &enabledSeen),
    "enabled-qlog receive packet was rejected");
  ZuCHECK(enabledSeen, "enabled qlog did not construct receive accumulator");

  ZmRef<ZiIOBuf> bad = shortPing(cid, secret, 3, false);
  ZuCHECK(bad && bad->length, "runtime qlog corrupt packet setup failed");
  bad->data_()[bad->length - 1] ^= 0x01;
  ZuCHECK(!link->receiveShort(ZuMv(bad)),
    "runtime qlog corrupt packet was accepted");

	closeQLog(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());
  link->teardown();

  ZuCHECK(diag.recordsEnqueued >= 2, "runtime qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 4, "runtime qlog write mismatch");
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
  ZuCHECK(data.find<"zquic:packet_protection_failed">() >= 0,
    "runtime packet protection failure qlog missing");
  ZuCHECK(data.find<"protection">() >= 0,
    "runtime packet protection failure reason missing");

  ZiFile::remove(path);
}

void testFlowControlQLog()
{
  ZuTestScope(testFlowControlQLog);

  Zi::Path path = testPath("ZquicStreamFlowControlQLog.sqlog");
  ZiFile::remove(path);

	ZquicLogParams params;
	params.enabled(true).path(path).ringSize(1<<15);

	App app;
	ZuCHECK(ZquicLogger::init(
	    app.qlogTrace(), params, Zquic::Vantage::Unknown),
	  "flow-control qlog init failed");
	ZquicLogger::start();
	ZuCHECK(ZquicLogger::enabled(), "flow-control qlog did not enable");
	ZmRef<TestLink> link = testLink(&app);
  ZmRef<TestStream> stream = link->stream(Zquic::StreamType::Duplex);
  ZuCHECK(stream, "flow-control qlog stream open failed");
  stream->txCredit(32);
  {
    auto tx = stream->txStream_();
    tx << "connection-blocked" << Zi::flush();
  }
  ZuCHECK(!link->flushCongestedStream(stream) &&
      stream->txRangeCount() &&
      link->queuedControlFrames() == 1,
    "flow-control qlog connection data-credit block not reached");
  link->grantDataCredit(32);
  ZuCHECK(!link->queuedControlFrames(),
    "flow-control qlog MAX_DATA did not clear DATA_BLOCKED");
  ZuCHECK(link->flushCongestedStream(stream),
    "flow-control qlog stream did not send after MAX_DATA");

  ZmRef<TestStream> streamBlocked = link->stream(Zquic::StreamType::Duplex);
  ZuCHECK(streamBlocked, "flow-control qlog blocked stream open failed");
  streamBlocked->txCredit(0);
  {
    auto tx = streamBlocked->txStream_();
    tx << "blocked" << Zi::flush();
  }
  ZuCHECK(!link->flushCongestedStream(streamBlocked) &&
      streamBlocked->txRangeCount() &&
      link->queuedControlFrames() == 1,
    "flow-control qlog stream data-credit block not reached");
  link->grantStreamCredit(streamBlocked, 7);
  ZuCHECK(!link->queuedControlFrames(),
    "flow-control qlog MAX_STREAM_DATA did not clear STREAM_DATA_BLOCKED");

	closeQLog(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());
  link->teardown();

  ZuCHECK(diag.recordsEnqueued >= 4, "flow-control qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 5, "flow-control qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "flow-control qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "flow-control qlog output was not written");
  ZuCHECK(parseJSONSeq(data) >= 5, "flow-control qlog JSON-SEQ parse failed");
  ZuCHECK(data.find<"quic:connection_data_blocked_updated">() >= 0,
    "flow-control qlog missing connection_data_blocked_updated");
  ZuCHECK(data.find<"quic:stream_data_blocked_updated">() >= 0,
    "flow-control qlog missing stream_data_blocked_updated");
  ZuCHECK(data.find<"connection_flow_control">() >= 0,
    "flow-control qlog missing connection blocked reason");
  ZuCHECK(data.find<"stream_flow_control">() >= 0,
    "flow-control qlog missing stream flow reason");
  ZuCHECK(data.find<"\"new\":\"blocked\"">() >= 0,
    "flow-control qlog missing blocked state");
  ZuCHECK(data.find<"\"old\":\"blocked\"">() >= 0 &&
      data.find<"\"new\":\"unblocked\"">() >= 0,
    "flow-control qlog missing unblocked state");

  ZiFile::remove(path);
}

void testPTOQLog()
{
  ZuTestScope(testPTOQLog);

  Zi::Path path = testPath("ZquicStreamPTOQLog.sqlog");
  ZiFile::remove(path);

	ZquicLogParams params;
	params.enabled(true).path(path).ringSize(1<<15);

	App app;
	ZuCHECK(ZquicLogger::init(
	    app.qlogTrace(), params, Zquic::Vantage::Unknown),
	  "PTO qlog init failed");
	ZquicLogger::start();
	ZmRef<TestLink> link = testLink(&app);
  link->confirmHandshake();
  link->recordSentPkt(
    Zquic::PktNumSpace::AppData, 7, Zquic::MinUDPPayload,
    Zquic::SentFrameRef::control(), true);
  link->sendAckEliciting(10);
  link->ackOnly(10);

  link->recordSentPkt(
    Zquic::PktNumSpace::AppData, 20, Zquic::MinUDPPayload,
    Zquic::SentFrameRef::control(), true);
  Zquic::PktNumSpace::T level = Zquic::PktNumSpace::Initial;
  unsigned probes = 0;
  ZuCHECK(link->forcePTOReclaimQLog(level, probes) && probes,
    "PTO qlog reclaim did not fire");
  link->forcePTOProbeQLog(level, probes);
  link->teardown();

	closeQLog(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());

  ZuCHECK(diag.recordsEnqueued >= 3, "PTO qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 4, "PTO qlog write mismatch");
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
}

void testPeerKeyUpdateState()
{
  ZuTestScope(testPeerKeyUpdateState);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  Zquic::CxnID cid{"keycid01"};
  Zquic::TrafficSecret initial;
  Zquic::TrafficSecret next;
  Zquic::TrafficSecret third;
  ZuCHECK(trafficSecret(initial, 7), "initial 1-RTT secret derivation failed");
  ZuCHECK(Zquic::PktProt::deriveNextSecret(next, initial),
    "next 1-RTT secret derivation failed");
  ZuCHECK(Zquic::PktProt::deriveNextSecret(third, next),
    "third 1-RTT secret derivation failed");
  ZuCHECK(link->installOneRTT(initial, initial, cid),
    "test 1-RTT secret install failed");

  bool currentOK = link->receiveShort(shortPing(cid, initial, 1, false));
  bool updateOK = link->receiveShort(shortPing(cid, next, 2, true));
  Zquic::LinkDiag updateDiag = link->diag();
  bool oldOK = link->receiveShort(shortPing(cid, initial, 0, false));
  Zquic::LinkDiag oldDiag = link->diag();
  bool rapidOK = link->receiveShort(shortPing(cid, third, 3, false));
  Zquic::LinkDiag invalidDiag = link->diag();
  link->discardPeerKeys();
  Zquic::LinkDiag discardDiag = link->diag();
  bool discardedOK = link->receiveShort(shortPing(cid, initial, 4, false));
  link->teardown();

  ZuCHECK(currentOK, "current key packet was rejected");
  ZuCHECK(updateOK, "peer key update packet was rejected");
  ZuCHECK(updateDiag.rx.peerKeyUpdates == 1 && updateDiag.rx.packetsRx == 2 &&
      !updateDiag.rx.invalidKeyPhases,
    "peer key update diagnostics mismatch");
  ZuCHECK(oldOK, "old key reorder packet was rejected");
  ZuCHECK(oldDiag.rx.oldKeysAccepted == 1 && oldDiag.rx.peerKeyUpdates == 1,
    "old key retention diagnostics mismatch");
  ZuCHECK(!rapidOK, "rapid second key update was accepted");
  ZuCHECK(invalidDiag.rx.invalidKeyPhases == 1 &&
      invalidDiag.rx.peerKeyUpdates == 1,
    "invalid key phase diagnostics mismatch");
  ZuCHECK(discardDiag.rx.keyDiscards == 1, "key discard diagnostic mismatch");
  ZuCHECK(!discardedOK, "discarded old key packet was accepted");
}
#endif

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("ZquicStreamTest");
#ifdef Zquic_DEBUG
  g_residue = ZiTestResidue::dir("qlog");
#endif
  ZuTestMain();
  ZuTestCall(testStreamIDs);
  ZuTestCall(testStreamFrameDelivery);
  ZuTestCall(testStreamRxSliceDelivery);
  ZuTestCall(testOutOfOrderStreamDelivery);
  ZuTestCall(testRxRetirement);
  ZuTestCall(testStreamTxRetention);
  ZuTestCall(testStreamTxRetainedBuffers);
  ZuTestCall(testStreamTxQueueLimit);
  ZuTestCall(testStreamTxUnackd);
  ZuTestCall(testStreamTxUnackdFin);
  ZuTestCall(testLinkStreamTxUnackdAck);
  ZuTestCall(streamRetxUnackdIdempotent);
  ZuTestCall(testStreamPktizer);
  ZuTestCall(testStreamProducerCredit);
  ZuTestCall(protectedPathResponseSize);
  ZuTestCall(testStreamRetxPacketBudget);
  ZuTestCall(testCtlSendFailRetainsFrame);
  ZuTestCall(testMultiFrameAssembly);
  ZuTestCall(streamRetxClipsUnackd);
  ZuTestCall(testStreamRetxClipsUnackdFin);
  ZuTestCall(cryptoRetxClipsUnackd);
  ZuTestCall(testCryptoRetxBudget);
  ZuTestCall(testPTOMultipleSpaces);
  ZuTestCall(testPktNumLength);
  ZuTestCall(testLongHeaderCoalescing);
  ZuTestCall(congestionBudgetGatesSends);
  ZuTestCall(ackCreditServicesRetx, true);
  ZuTestCall(ackCreditServicesRetx, false);
  ZuTestCall(testStreamBatchContinuation);
  ZuTestCall(testActivePathBudget);
  ZuTestCall(pathValidationStateMachine);
  ZuTestCall(passivePathChallengeRecovery);
#ifdef Zquic_DEBUG
  ZuTestCall(testPMTUDQLog);
  ZuTestCall(testCIDQLog);
  ZuTestCall(testAckECNValidateDisable);
  ZuTestCall(testRxQLog);
  ZuTestCall(testFlowControlQLog);
  ZuTestCall(testPTOQLog);
#endif
  ZuTestCall(testBlockedFrameDedup);
  ZuTestCall(testKeyedControlReplacement);
  ZuTestCall(testControlInvalidation);
  ZuTestCall(testPeerStreamAcceptance);
  ZuTestCall(testStreamCountLimits);
  ZuTestCall(testResetStopFrames);
  ZuTestCall(testLocalResetStopSend);
  ZuTestCall(testMaxBlockedValidate);
  ZuTestCall(testStreamBlockedValidate);
  ZuTestCall(invalidClosedStreamActivity);
  ZuTestCall(testStreamGC);
  ZuTestCall(frameRoleAndSpaceLegality);
  ZuTestCall(testZeroRTTProtectedSend);
  ZuTestCall(testZeroRTTEarlyStreamPolicy);
#ifdef Zquic_DEBUG
  ZuTestCall(testZeroRTTAcceptQLog);
  ZuTestCall(zeroRTTAfterOneRTTRejected);
  ZuTestCall(testPeerKeyUpdateState);
#endif
}
