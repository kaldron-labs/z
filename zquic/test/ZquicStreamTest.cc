//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZtJSON.hh>
#include <zlib/ZiFile.hh>
#include <zlib/Zquic.hh>

#include <zpicotls/openssl.h>

using namespace ZuTestUtil;

using StreamTxBufAlloc = Zquic::StreamTxBufAlloc<>;

static ZuBSpan span_(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
}

static void closeQLog_(ZquicLogger::Trace &trace)
{
  ZmBlock<>{}([&trace](auto wake) {
    ZquicLogger::close(trace, ZuMv(wake));
  });
}

struct App : public Zquic::Engine<App> {
  using Base = Zquic::Engine<App>;

  App(unsigned maxUDP = Zquic::MinUDPPayload) : m_mx{mxParams_()} {
    bool ok = m_mx.start();
    ZiAssert(ok, "Zquic", (), "stream test multiplexer start failed", return);
    if (ok)
      ok = Base::init(Zquic::EngineParams(&m_mx, "3", "4").maxUDP(maxUDP));
    ZiAssert(ok, "Zquic", (), "stream test app init failed", return);
  }
  ~App() {
    Base::final();
    m_mx.stop();
  }

  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
  template <typename L> void rxRun(L l) { l(); }
  template <typename L> void rxInvoke(L l) { l(); }
  template <typename L> void txRun(L l) { l(); }
  template <typename L> void txInvoke(L l) { l(); }
  template <typename O, typename L> void txInvoke(O *, L l) { l(); }
  template <typename Link>
  bool allowEarlyStream(Link *, uint64_t streamID, bool fin) {
    ++earlyStreamChecks;
    lastEarlyStreamID = streamID;
    lastEarlyStreamFin = fin;
    return earlyStreamAllowed;
  }

  bool		earlyStreamAllowed = false;
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

  unsigned processed = 0;
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
struct TestLink :
  public Zquic::Link<App, TestLink,
    StreamTxBufAlloc, TestStream>
{
  using Base = Zquic::Link<App, TestLink,
    StreamTxBufAlloc, TestStream>;
  TestLink(App *app, bool isServer = false) : Base{app, isServer} {
    Base::configureLocalTransportParams_(app);
  }
	  void resetRuntimeForTest() {
	    Base::resetRuntime_();
	    Base::configureLocalTransportParams_(app());
	  }
	  void closeForTest(uint64_t errorCode = 0) {
	    Base::closeState_(errorCode);
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
    return Base::queueBlocked_(Zquic::FrameType::DataBlocked, 0, value);
  }
  bool queueMaxData(uint64_t value) {
    return Base::txQueueControl_(Zquic::ControlFrame::flowUpdate(
      Zquic::FlowUpdate{
	Zquic::FrameType::MaxData, 0, value, Zi::StreamType::Duplex}));
  }
  bool queueMaxStreamData(uint64_t streamID, uint64_t value) {
    return Base::txQueueControl_(Zquic::ControlFrame::flowUpdate(
      Zquic::FlowUpdate{
	Zquic::FrameType::MaxStreamData, streamID, value,
	Zi::StreamType::Duplex}));
  }
  bool queueStreamDataBlocked(uint64_t streamID, uint64_t value) {
    return Base::queueBlocked_(
      Zquic::FrameType::StreamDataBlocked, streamID, value);
  }
  bool queueStreamsBlocked(Zi::StreamType::T type, uint64_t value) {
    return Base::queueBlocked_(
      Zquic::FrameType::StreamsBlocked, 0, value, type);
  }
  void scheduleStream(const ZmRef<TestStream> &stream) {
    Base::streamWritable_(stream);
  }
  unsigned scheduledStreams() const { return Base::scheduledStreamCount_(); }
  void grantDataCredit(uint64_t value) { Base::txApplyMaxData_(value); }
  void grantStreamCredit(const ZmRef<TestStream> &stream, uint64_t value) {
    Base::txApplyMaxStreamData_(stream, value);
  }
  bool applyMaxStreams(const Zquic::Frame &frame) {
    return frame.type == Zquic::FrameType::MaxStreams &&
      Base::txApplyMaxStreams_(frame.streamType, frame.value);
  }
  bool flushControlSendFails() {
    return Base::flushControlAndStreams_(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [](Zquic::PktBuild &, ZiSockAddr, const typename Base::TxPktRefs &) {
	return false;
      });
  }
  bool flushControlSends() {
    return Base::flushControlAndStreams_(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [](Zquic::PktBuild &, ZiSockAddr, const typename Base::TxPktRefs &) {
	return true;
      });
  }
  unsigned flushFrameRefs(
    Zquic::SentFrameKind::T *kinds, uint64_t *streamIDs,
    unsigned capacity) {
    unsigned refs = 0;
    bool ok = Base::flushControlAndStreams_(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [&refs, &kinds, &streamIDs, capacity](
	  Zquic::PktBuild &, ZiSockAddr, const typename Base::TxPktRefs &r) {
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
    bool ok = Base::flushControlAndStreams_(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [&n, refs, capacity](
	  Zquic::PktBuild &, ZiSockAddr, const typename Base::TxPktRefs &r) {
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
    Base::recordTxPkt_(
      level, pn, bytes, ref, ackEliciting, false, 0,
      Zquic::PktNumSpace::N, 0, packetType, ecn);
    Base::setTxPN_(level, pn + 1);
  }
  void recordSentPkt(
    Zquic::PktNumSpace::T level, uint64_t pn, unsigned bytes,
    const typename Base::TxPktRefs &refs, bool ackEliciting,
    Zquic::PktType::T packetType = Zquic::PktType::N) {
    Base::recordTxPkt_(
      level, pn, bytes, refs, ackEliciting, false, 0,
      Zquic::PktNumSpace::N, 0, packetType);
    Base::setTxPN_(level, pn + 1);
  }
  unsigned flushRecordedRefs(Zquic::SentFrameRef *refs, unsigned capacity) {
    unsigned n = 0;
    bool ok = Base::flushControlAndStreams_(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [this, &n, refs, capacity](Zquic::PktBuild &build, ZiSockAddr,
	  const typename Base::TxPktRefs &r) {
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
    const Zquic::SentFrameRef &ref, Zquic::Frame &frame) {
    Zquic::PktBuild build;
    if (!Base::buildRetransmitControl_(build, ref) || !build.count())
      return false;
    unsigned used = 0;
    return !Zquic::FrameCodec::parse(
      ZuBSpan{build.data()[0].base, unsigned(build.data()[0].len)},
      frame, used) && used == build.data()[0].len;
  }
  bool flushCongestedStream(ZmRef<TestStream> stream) {
    return Base::sendQueuedStreamPkt_(
      ZuMv(stream), ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [this](
	  Zquic::PktBuild &build, ZiSockAddr,
	  const typename Base::TxPktRefs &refs) {
	recordSentPkt(
	  Zquic::PktNumSpace::AppData, sentPkts, build.bytes(), refs, true);
	++sentPkts;
	return true;
      });
  }
  void fillCwnd() {
    Zquic::RuntimeDiag diag = Base::runtimeDiag_();
    while (diag.tx.congestionBytesInFlight < diag.tx.congestionWindow) {
      uint64_t remaining =
	diag.tx.congestionWindow - diag.tx.congestionBytesInFlight;
      unsigned bytes = remaining > app()->maxUDP() ?
	app()->maxUDP() : unsigned(remaining);
      recordSentPkt(
	Zquic::PktNumSpace::AppData, sentPkts, bytes,
	Zquic::SentFrameRef::control(), true);
      ++sentPkts;
      diag = Base::runtimeDiag_();
    }
  }
  void ackThrough(uint64_t pn) {
    Base::AckSnapshot ack;
    ack.level = Zquic::PktNumSpace::AppData;
    ack.nRanges = 1;
    ack.ranges[0] = Zquic::AckRange{pn, 0};
    Base::processAckFrameTx_(ack);
  }
  void ackOnly(uint64_t pn) {
    Base::AckSnapshot ack;
    ack.level = Zquic::PktNumSpace::AppData;
    ack.nRanges = 1;
    ack.ranges[0] = Zquic::AckRange{pn, pn};
    Base::processAckFrameTx_(ack);
  }
	  bool nextRetransmitRef(
	    Zquic::PktNumSpace::T &level, Zquic::SentFrameRef &ref) {
	    return Base::nextRetransmit_(level, ref);
	  }
	  bool forcePTOReclaimQLog(
	    Zquic::PktNumSpace::T &level, unsigned &probes) {
	    Base::notePTOExpired_();
	    return Base::reclaimPTO_(level, probes);
	  }
	  void forcePTOProbeQLog(Zquic::PktNumSpace::T level, unsigned probes) {
	    Base::notePTOProbe_(level, probes);
	  }
	  bool sendCryptoBytes(ZuBSpan data) {
	    size_t offsets[5] = {0, 0, 0, 0, data.length()};
	    return Base::sendCryptoFlights_(
      data.data(), data.length(),
      offsets, 900, ZiSockAddr{},
      [this](
	  Zquic::PktNumSpace::T level, ZuBSpan prefix, ZuBSpan payload,
	  const Zquic::SentFrameRef &ref, ZiSockAddr) {
	recordSentPkt(
	  level, sentPkts, prefix.length() + payload.length(), ref, true);
	++sentPkts;
	return true;
      });
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
    Zquic::PktNumSpace::T level, const Zquic::SentFrameRef &ref,
    Zquic::Frame &frame, uint8_t *b, unsigned size) {
    Zquic::PktBuild build;
    if (!Base::buildRetransmitCrypto_(level, build, ref)) return false;
    unsigned n = 0;
    for (unsigned i = 0; i < build.count(); ++i) {
      if (build.data()[i].len > size - n) return false;
      memcpy(b + n, build.data()[i].base, build.data()[i].len);
      n += unsigned(build.data()[i].len);
    }
    unsigned used = 0;
    return !Zquic::FrameCodec::parse(
      ZuBSpan{b, n}, frame, used) &&
      used == n;
  }
  void cancelTimers() { Base::cancelTimers_(); }
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
      Base::recordProtPktTx_(
	Zquic::PktNumSpace::AppData, i, 1, {}, nullptr, false);
  }
#ifdef Zquic_DEBUG
  void forcePN(uint64_t pn) {
    Base::setTxPN_(Zquic::PktNumSpace::AppData, pn);
  }
  bool addLocalCIDForQLog(
    const Zquic::CxnID &id, uint64_t sequence,
    const Zquic::ResetToken &token = {}) {
    return Base::addLocalCID_(id, sequence, token);
  }
  bool receiveNewCxnIDForQLog(const Zquic::Frame &frame) {
    return Base::receiveNewCxnID_(frame);
  }
  bool receiveRetireCxnIDForQLog(const Zquic::Frame &frame) {
    return Base::receiveRetireCxnID_(frame);
  }
  void installLocalCIDRoutesForQLog(TestCIDRoutes &routes) {
    Base::installLocalCIDRoutes_(routes);
  }
  void retireLocalCIDRoutesForQLog(TestCIDRoutes &routes) {
    Base::retireLocalCIDRoutes_(routes);
  }
  void tombstoneLocalCIDRoutesForQLog(TestCIDRoutes &routes) {
    Base::tombstoneLocalCIDRoutes_(routes);
  }
#endif
  unsigned pnLength() const {
    return Base::txPNLength_(Zquic::PktNumSpace::AppData);
  }
  bool coalesceProbe(unsigned &sends, unsigned &bytes) {
    sends = bytes = 0;
    ZmRef<ZiIOBuf> initial = new Zquic::PktTxBufAlloc<>{nullptr};
    ZmRef<ZiIOBuf> handshake = new Zquic::PktTxBufAlloc<>{nullptr};
    initial->skip = handshake->skip = 0;
    initial->length = 100;
    handshake->length = 40;
    memset(initial->data_(), 0x11, initial->length);
    memset(handshake->data_(), 0x22, handshake->length);
    Base::beginLongCoalesce_();
    bool ok = Base::holdInitialForCoalesce_(
      ZuMv(initial), ZiSockAddr{},
      [&sends, &bytes](auto buf, ZiSockAddr) {
	++sends;
	bytes += buf->length;
	return true;
      });
    ok = Base::sendHandshakeCoalesced_(
      ZuMv(handshake), ZiSockAddr{},
      [&sends, &bytes](auto buf, ZiSockAddr) {
	++sends;
	bytes += buf->length;
	return buf->length == 140 &&
	  buf->data_()[99] == 0x11 &&
	  buf->data_()[100] == 0x22 &&
	  buf->data_()[139] == 0x22;
      }) && ok;
    Base::endLongCoalesce_();
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
    Base::setTxPN_(Zquic::PktNumSpace::AppData, largest + 1);
    Base::AckSnapshot ack;
    ack.level = Zquic::PktNumSpace::AppData;
    ack.nRanges = 1;
    ack.ranges[0] = Zquic::AckRange{largest, 0};
    ack.ecn.ect0 = ect0;
    ack.ecn.ect1 = ect1;
    ack.ecn.ce = ce;
    Base::processAckFrameTx_(ack);
  }
  void ackNoECN(uint64_t largest) {
    Base::setTxPN_(Zquic::PktNumSpace::AppData, largest + 1);
    Base::AckSnapshot ack;
    ack.level = Zquic::PktNumSpace::AppData;
    ack.nRanges = 1;
    ack.ranges[0] = Zquic::AckRange{largest, 0};
    Base::processAckFrameTx_(ack);
  }
#endif
  bool receiveMarked(
    uint64_t pn, Zquic::EcnMark::T ecn, bool ackEliciting = true) {
    if (Base::rxPktSeen_(Zquic::PktNumSpace::AppData, pn)) return false;
    Base::noteAck_(
      Zquic::PktNumSpace::AppData, pn, ackEliciting, ZiSockAddr{}, false, ecn);
    return true;
  }
  bool writePendingAck(Zquic::PktBuild &build) {
    return Base::appendPendingAck_(Zquic::PktNumSpace::AppData, build);
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
    Base::startPathValidation_(ZuMv(local), ZuMv(remote));
  }
  bool validatingPath() const { return Base::validatingPath_(); }
  ZuBSpan validatingChallenge() const {
    return Base::validatingChallenge_();
  }
#endif
  bool pathResponse(ZuBSpan data) { return Base::onPathResponse_(data); }
  void pathTimeout() { Base::pathExpired_(); }
  const ZiSockAddr &activePathRemote() const {
    return Base::activePathRemote_();
  }
  void pathReceived(unsigned bytes) { Base::recordPathRxTx_(ZiSockAddr{}, bytes); }
  bool pathSend(unsigned bytes) {
    ZmRef<ZiIOBuf> buf = new Zquic::PktTxBufAlloc<>{nullptr};
    buf->skip = 0;
    buf->length = bytes;
    return Base::sendPathPkt_(
      ZuMv(buf), ZiSockAddr{},
      [](ZmRef<ZiIOBuf>, ZiSockAddr, Zquic::EcnMark::T) { return true; });
  }
  void validatePath() { Base::validatePathTx_(); }
#ifdef Zquic_DEBUG
  void growActivePath(unsigned size) { Base::forceActivePathMTU_(size); }
  bool startPMTUDProbe(unsigned size) {
    return Base::startPMTUDProbeChecked_(size);
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
  uint64_t pathAntiAmplification() const {
    return Base::pathAntiAmplification_();
  }
  unsigned activePathMaxUDP() const { return Base::activePathMaxUDP_(); }
  Zquic::RuntimeDiag runtimeDiag() const { return Base::runtimeDiag_(); }
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
    ack.nRanges = 1;
    ack.ranges[0] = Zquic::AckRange{largest, first};
    return Base::ackFrameValidTx_(ack);
  }
#ifdef Zquic_DEBUG
  bool installOneRTT(
    const Zquic::TrafficSecret &rx, const Zquic::TrafficSecret &tx,
    const Zquic::CxnID &localCID) {
    return Base::installAppDataKeys_(rx, tx, localCID);
  }
  void discardPeerKeys() { Base::discardPeerKeys_(); }
#endif
	  bool installZeroRTTTx(const Zquic::TrafficSecret &tx) {
	    return Base::txInstallKeyTrafficSecret_(Zquic::PktKeyLevel::ZeroRTT, tx);
	  }
	  bool installZeroRTTRx(const Zquic::TrafficSecret &rx) {
	    return Base::rxInstallKeyTrafficSecret_(Zquic::PktKeyLevel::ZeroRTT, rx);
	  }
	  Zquic::LinkEarlyState::T rxEarlyState() const {
	    return Base::rxEarlyDataState_();
	  }
	  Zquic::LinkEarlyState::T txEarlyState() const {
	    return Base::txEarlyDataState_();
	  }
	  bool acceptZeroRTTRx() { return Base::acceptEarlyDataRx_(); }
	  bool acceptZeroRTTTx() { return Base::acceptEarlyDataTx_(); }
	  bool rxOneRTTSeen() const { return Base::rxOneRTTSeen_(); }
	  bool flushEarlyStreamRefs(Zquic::SentFrameRef *refs, unsigned capacity) {
    unsigned n = 0;
    bool ok = Base::flushEarlyStreams_(
      ZiSockAddr{},
      [this, &n, refs, capacity](Zquic::PktBuild &build, ZiSockAddr,
	  const typename Base::TxPktRefs &r) {
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
    return Base::rejectZeroRTTTx_(Zquic::ZeroRTTReason::TLSRejected);
  }
  bool canTxZeroRTT() const { return Base::canTxZeroRTT_(); }
  bool sendZeroRTTProbe(ZmRef<ZiIOBuf> &sent) {
    uint8_t ping[1];
    int n = Zquic::FrameCodec::writePing(ping, sizeof(ping));
    if (n != 1) return false;
    Zquic::PktBuild payload;
    if (!payload.add(span_(ping, unsigned(n)))) return false;
    return Base::sendProtZeroRTTPkt_(
      Base::RuntimeCID::Peer, Base::RuntimeCID::Local,
      Base::txPNLength_(Zquic::PktNumSpace::AppData),
      payload, ZiSockAddr{}, span_(ping, unsigned(n)), nullptr, true,
      []() { return new Zquic::PktTxBufAlloc<>{nullptr}; },
      [&sent](auto buf, ZiSockAddr) {
	sent = ZuMv(buf);
	return true;
      });
  }
	  bool receiveShort(ZmRef<ZiIOBuf> buf, bool *qlogSeen = nullptr) {
    if (!buf) return false;
    Zquic::Datagram d;
    d.buf = ZuMv(buf);
    return Base::receiveProtShortPkt_(
      d, 0, d.buf->length,
      [this, qlogSeen](
	  Zquic::PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
		  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
		  typename Base::RxAckMeta &ack, auto *qlog) {
	if (qlogSeen) *qlogSeen = qlog != nullptr;
	return Base::consumeProtFrames_(
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
	    return Base::receiveProtLongPkt_(
	      Base::InitialKeyDir::Server, d, 0, d.buf->length,
	      [](const Zquic::LongHdr &, Zquic::Datagram &) { return true; },
	      [this](
		  Zquic::PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
		  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
		  typename Base::RxAckMeta &ack, auto *qlog, bool earlyData) {
		return Base::consumeProtFrames_(
		  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog,
		  [](size_t, ZuBSpan, ZiSockAddr) { return true; },
		  [](Zquic::PktNumSpace::T, const Zquic::Frame &, ZiSockAddr) {
		    return true;
		  },
		  earlyData);
	      });
	  }
  void flushTx_() { ++txFlushQueued; }
  void flushTx_(ZiSockAddr) { ++txFlushQueued; }
  void queueTxFlush_() { ++txFlushQueued; }
  void queueTxFlush_(ZiSockAddr) { ++txFlushQueued; }
  void pto_() { ++ptos; }
  void queueRetransmit_() { ++retransmits; }
  bool retransmit_() { ++retransmits; return false; }

  ZmRef<TestStream>	lastStream;
  unsigned		streamedCount = 0;
  uint64_t		sentPkts = 0;
  unsigned		txFlushQueued = 0;
  unsigned		ptos = 0;
  unsigned		retransmits = 0;
};

static ZmRef<TestLink> testLink(App *app, bool isServer = false)
{
  ZmRef<TestLink> link = new TestLink{app, isServer};
  link->resetRuntimeForTest();
  return link;
}

static bool trafficSecret_(
  Zquic::TrafficSecret &secret, uint8_t seed)
{
  uint8_t bytes[32];
  for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = seed + i;
  return Zquic::PktProt::deriveTrafficSecret(
    secret, &ptls_openssl_aes128gcmsha256, span_(bytes, sizeof(bytes)));
}

static ZmRef<ZiIOBuf> shortPing_(
  const Zquic::CxnID &dcid, const Zquic::TrafficSecret &secret,
  uint64_t pn, bool keyPhase)
{
  Zquic::PktProtState tx;
  if (!tx.init(secret, Zquic::PktNumSpace::AppData, true)) return {};
  ZmRef<ZiIOBuf> buf = new Zquic::PktTxBufAlloc<>{nullptr};
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

static ZmRef<ZiIOBuf> zeroRTTLongHdr_(uint64_t pn)
{
  ZmRef<ZiIOBuf> buf = new Zquic::PktRxBufAlloc<>{nullptr};
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

static bool parseFrame_(const uint8_t *b, int n, Zquic::Frame &frame)
{
  unsigned used = 0;
  return n > 0 && !Zquic::FrameCodec::parse(
    ZuBSpan{b, unsigned(n)}, frame, used) &&
    used == unsigned(n);
}

static ZmRef<ZiIOBuf> streamPkt_(
  uint64_t id, uint64_t offset, ZuBSpan payload, bool fin,
  Zquic::Frame &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = new Zquic::PktRxBufAlloc<>{nullptr};
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
    [&remaining](ZuBSpan span) -> int64_t {
      if (remaining > span.length()) {
	remaining -= span.length();
	return 0;
      }
      return remaining;
    },
    [&called, &ok, &expected](ZuBSpan span) {
      called = true;
      ok = span.length() == expected.length() &&
	!memcmp(span.data(), expected.data(), expected.length());
    });
  return consumed == n && called && ok;
}

static ZiIP ip4_(uint32_t n)
{
  in_addr addr;
  addr.s_addr = htonl(n);
  return ZiIP{addr};
}

static Zi::Path testPath_(ZuCSpan name)
{
  Zi::Path path;
  path << name;
  return path;
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

static void removeTestLog_(const Zi::Path &path)
{
  if (!::getenv("ZQUIC_TEST_KEEP")) ZiFile::remove(path);
}

static unsigned parseJSONSeq_(ZuCSpan data)
{
  unsigned n = 0;
  unsigned i = 0;
  while (i < data.length()) {
    ZuCSpan rest{data.data() + i, data.length() - i};
    if (!rest.match<"\x1e">()) return 0;
    ++i;
    unsigned start = i;
    while (i < data.length() && data[i] != '\n') ++i;
    if (i >= data.length()) return 0;
    ZtString<> json;
    json << ZuCSpan{data.data() + start, i - start};
    auto scan = ZtJSON::scan(json);
    if (scan.p<0>() != int(json.length())) return 0;
    ++n;
    ++i;
  }
  return n;
}

void testStreamIDs()
{
  ZuTestScope(testStreamIDs);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto c0 = client->stream(Zi::StreamType::Duplex);
  auto c1 = client->stream(Zi::StreamType::Simplex);
  ZuCHECK(c0->id() == 0 && c1->id() == 2, "client stream IDs mismatch");

  ZmRef<TestLink> server = testLink(&app, true);
  auto s0 = server->stream(Zi::StreamType::Duplex);
  auto s1 = server->stream(Zi::StreamType::Simplex);
  ZuCHECK(s0->id() == 1 && s1->id() == 3, "server stream IDs mismatch");
  ZuCHECK(server->findStream(1) == s0, "stream table lookup failed");
}

void testStreamFrameDelivery()
{
  ZuTestScope(testStreamFrameDelivery);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto stream = client->stream(Zi::StreamType::Duplex);

  Zquic::Frame frame;
  unsigned used = 0;
  auto packet = streamPkt_(stream->id(), 0, "hello", false, frame, used);
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

  packet = streamPkt_(stream->id(), 5, {}, true, frame, used);
  ZuCHECK(packet, "STREAM FIN frame setup failed");
  ZuCHECK(stream->processFrame(frame, packet, &diag) == 0 &&
      stream->finReceived() && stream->rxComplete() &&
      stream->finalSize() == 5,
    "STREAM FIN delivery mismatch");

  packet = streamPkt_(stream->id(), 5, "!", true, frame, used);
  ZuCHECK(packet,
    "conflicting STREAM FIN setup failed");
  ZuCHECK(!stream->receiveFrame(frame, packet, &diag),
    "conflicting final-size STREAM was accepted");

  packet = streamPkt_(stream->id() + 4, 0, "x", false, frame, used);
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
  auto stream = client->stream(Zi::StreamType::Duplex);

  ZmRef<ZiIOBuf> packet = new Zquic::PktRxBufAlloc<>{nullptr};
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
  auto stream = client->stream(Zi::StreamType::Duplex);

  Zquic::Frame frame;
  unsigned used = 0;
  Zquic::BufDiag diag;

  auto packet = streamPkt_(stream->id(), 5, "world", false, frame, used);
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

  packet = streamPkt_(stream->id(), 0, "hello", false, frame, used);
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

  auto split = client->stream(Zi::StreamType::Duplex);
  diag = {};
  packet = streamPkt_(split->id(), 5, "world", false, frame, used);
  ZuCHECK(packet,
    "split pending STREAM setup failed");
  ZuCHECK(split->processFrame(frame, packet, &diag) == 0 &&
      split->rxPending() == 1 &&
      !split->rxQueued(),
    "split pending STREAM state mismatch");

  packet = streamPkt_(split->id(), 0, "helloworldtails", false, frame, used);
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

void testStreamTxRetention()
{
  ZuTestScope(testStreamTxRetention);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  auto stream = client->stream(Zi::StreamType::Duplex);

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

void testStreamTxUnackd()
{
  ZuTestScope(testStreamTxUnackd);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zi::StreamType::Duplex);
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
  auto stream = link->stream(Zi::StreamType::Duplex);
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
  auto stream = link->stream(Zi::StreamType::Duplex);

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
  link->cancelTimers();
}

void testLinkStreamRetransmitUnackdIdempotent()
{
  ZuTestScope(testLinkStreamRetransmitUnackdIdempotent);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zi::StreamType::Duplex);

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
  link->cancelTimers();
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
  auto stream = client->stream(Zi::StreamType::Duplex);
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

  auto blocked = client->stream(Zi::StreamType::Duplex);
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

  auto split = client->stream(Zi::StreamType::Duplex);
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

void testQueuedControlSendFailureRetainsFrame()
{
  ZuTestScope(testQueuedControlSendFailureRetainsFrame);

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

void testRuntimeMultiFrameAssembly()
{
  ZuTestScope(testRuntimeMultiFrameAssembly);

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
  auto s0 = streams->stream(Zi::StreamType::Duplex);
  auto s1 = streams->stream(Zi::StreamType::Duplex);
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
  auto finStream = finOnly->stream(Zi::StreamType::Duplex);
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

void testStreamRetransmitClipsUnackd()
{
  ZuTestScope(testStreamRetransmitClipsUnackd);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zi::StreamType::Duplex);
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
  link->cancelTimers();
}

void testStreamRetransmitClipsUnackdFin()
{
  ZuTestScope(testStreamRetransmitClipsUnackdFin);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zi::StreamType::Duplex);
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
  link->cancelTimers();
}

void testCryptoRetransmitClipsUnackd()
{
  ZuTestScope(testCryptoRetransmitClipsUnackd);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  Zquic::TrafficSecret secret;
  ZuCHECK(trafficSecret_(secret, 0x42) &&
      link->installOneRTT(secret, secret, {}),
    "crypto retransmit key setup failed");
  link->initServerPath();
  link->growActivePath(Zquic::MinUDPPayload);
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
  link->cancelTimers();
}

void testRuntimePacketNumberLength()
{
  ZuTestScope(testRuntimePacketNumberLength);

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

void testCongestionBudgetGatesRuntimeSends()
{
  ZuTestScope(testCongestionBudgetGatesRuntimeSends);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zi::StreamType::Duplex);
  link->grantDataCredit(20000);
  stream->txCredit(20000);
  {
    auto tx = stream->txStream_();
    tx << "0123456789abcdef0123456789abcdef" << Zi::flush();
  }
  stream->fin();

  link->fillCwnd();
  Zquic::RuntimeDiag diag = link->runtimeDiag();
  ZuCHECK(diag.tx.congestionBytesInFlight >= diag.tx.congestionWindow &&
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
  diag = link->runtimeDiag();
  ZuCHECK(diag.tx.congestionBytesInFlight < diag.tx.congestionWindow &&
      link->txFlushQueued == queued + 1,
    "runtime ACK did not reopen congestion window and queue Tx flush");
  bool flushed = link->flushControlSends();
  ZuCHECK(flushed, "runtime control send did not resume after ACK opened cwnd");
  ZuCHECK(!link->queuedControlFrames(),
    "runtime control queue did not drain after ACK opened cwnd");
  link->closeForTest();
}

void testActivePathRuntimeBudget()
{
  ZuTestScope(testActivePathRuntimeBudget);

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
  Zi::Path path = testPath_("ZquicStreamPathBudgetQLog.sqlog");
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
      !server->pathAntiAmplification() &&
      !server->pathSend(1),
    "unvalidated server path sent without received bytes");

  server->pathReceived(400);
  ZuCHECK(server->pathAntiAmplification() == 1200 &&
      server->pathSend(1000) &&
      !server->pathSend(201) &&
      server->pathSend(200) &&
      !server->pathAntiAmplification(),
    "unvalidated server path did not enforce 3x send budget");

  Zquic::PathDiag diag = server->pathDiag();
  ZuCHECK(diag.bytesRx == 400 && diag.bytesTx == 1200,
    "active path byte accounting mismatch after budget exhaustion");

  server->pathReceived(100);
  ZuCHECK(server->pathAntiAmplification() == 300 &&
      server->pathSend(300),
    "received bytes did not expand server anti-amplification budget");

  server->validatePath();
  ZuCHECK(server->pathValidated() &&
      ZuCmp<uint64_t>::null(server->pathAntiAmplification()) &&
      server->pathSend(Zquic::MinUDPPayload),
    "address validation did not unlock active path send allowance");
  server->closeForTest();

#ifdef Zquic_DEBUG
	closeQLog_(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag qdiag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());
  ZuCHECK(qdiag.recordsEnqueued >= 4, "path-budget qlog enqueue mismatch");
  ZuCHECK(qdiag.recordsWritten >= 5, "path-budget qlog write mismatch");
  ZuCHECK(qdiag.writerFailures == 0, "path-budget qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "path-budget qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 5,
    "path-budget qlog JSON-SEQ parse failed");
	  ZuCHECK(data.find<"quic:packet_dropped">() >= 0,
	    "path-budget qlog missing packet_dropped");
	  ZuCHECK(data.find<"\"trigger\":\"rejected\"">() >= 0,
	    "path-budget qlog missing rejected trigger");
  ZiFile::remove(path);
#endif
}

void testPathValidationStateMachine()
{
  ZuTestScope(testPathValidationStateMachine);

  Zquic::PathChallenge challenge;
  ZuCHECK(challenge.generate() &&
      challenge.valid() &&
      challenge.length() == Zquic::PathChallenge::Length &&
      challenge.equals(challenge.bspan()),
    "PATH_CHALLENGE value generation failed");

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicStreamPathValidationQLog.sqlog");
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

	closeQLog_(app.qlogTrace());
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
  ZuCHECK(parseJSONSeq_(qlog) >= 7,
    "path-validation qlog JSON-SEQ parse failed");
  ZuCHECK(qlog.find<"quic:path_validated">() >= 0,
    "path-validation qlog event missing");
  ZuCHECK(qlog.find<"\"success\":true">() >= 0,
    "path-validation qlog success missing");
  ZuCHECK(qlog.find<"\"success\":false">() >= 0,
    "path-validation qlog failure missing");
  ZuCHECK(qlog.find<"\"vantage\":\"unknown\"">() >= 0,
    "path-validation qlog vantage point missing");
  removeTestLog_(path);
#endif
}

void testPMTUDQLog()
{
  ZuTestScope(testPMTUDQLog);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicStreamPMTUDQLog.sqlog");
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

	closeQLog_(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());
  ZuCHECK(diag.recordsEnqueued >= 2, "PMTUD qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 3, "PMTUD qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "PMTUD qlog writer failure");

  ZtString<> qlog = readFile_(path);
  ZuCHECK(qlog, "PMTUD qlog output missing");
  ZuCHECK(parseJSONSeq_(qlog) >= 3, "PMTUD qlog JSON-SEQ parse failed");
  ZuCHECK(qlog.find<"quic:mtu_updated">() >= 0,
    "PMTUD qlog missing mtu_updated");
  ZuCHECK(qlog.find<"\"new\":1400">() >= 0,
    "PMTUD qlog missing ACKed MTU");
  ZuCHECK(qlog.find<"\"new\":1500">() >= 0,
    "PMTUD qlog missing lost probe MTU");
  ZuCHECK(qlog.find<"\"done\":true">() >= 0,
    "PMTUD qlog missing completion flag");
  removeTestLog_(path);
#endif
}

#ifdef Zquic_DEBUG
void testCIDQLog()
{
  ZuTestScope(testCIDQLog);

  Zi::Path path = testPath_("ZquicStreamCIDQLog.sqlog");
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
  ZuCHECK(link->addLocalCIDForQLog(localCID, 1, token),
    "CID qlog local issue setup failed");

  TestCIDRoutes routes;
  link->installLocalCIDRoutesForQLog(routes);
  ZuCHECK(routes.adds >= 1, "CID qlog route install did not bind route");

  Zquic::Frame retire;
  retire.type = Zquic::FrameType::RetireCxnID;
  retire.value = 1;
  ZuCHECK(link->receiveRetireCxnIDForQLog(retire),
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
  ZuCHECK(link->receiveNewCxnIDForQLog(peer),
    "CID qlog peer CID 0 setup failed");
  peer.value = 1;
  peer.offset = 1;
  peer.length = peerCID1.length();
  peer.payload = peerCID1;
  peer.resetToken = token1;
  ZuCHECK(link->receiveNewCxnIDForQLog(peer),
    "CID qlog peer CID retire-prior-to setup failed");

  link->closeForTest();

	closeQLog_(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());

  ZuCHECK(diag.recordsEnqueued >= 5, "CID qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 6, "CID qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "CID qlog writer failure");

  ZtString<> qlog = readFile_(path);
  ZuCHECK(qlog, "CID qlog output missing");
  ZuCHECK(parseJSONSeq_(qlog) >= 6, "CID qlog JSON-SEQ parse failed");
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
  removeTestLog_(path);
}

void testAckECNValidationDisablesECN()
{
  ZuTestScope(testAckECNValidationDisablesECN);

  Zi::Path path = testPath_("ZquicStreamECNQLog.sqlog");
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
  Zquic::RuntimeDiag diag = link->runtimeDiag();
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
  diag = link->runtimeDiag();
  ZuCHECK(!link->ecnDisabled() &&
      diag.tx.peerAckECN[Zquic::PktNumSpace::AppData].ect0 == 2 &&
      diag.tx.peerAckECN[Zquic::PktNumSpace::AppData].ect1 == 1,
    "valid ACK_ECN did not update runtime diagnostics");
  link->ackECN(2, 1, 1, 0);
  diag = link->runtimeDiag();
  ZuCHECK(link->ecnDisabled() && diag.tx.ecnValidationFailures == 1,
    "regressing ACK_ECN did not disable ECN");
  link->closeForTest();

  ZmRef<TestLink> missing = testLink(&app);
  missing->enableECN();
  for (unsigned i = 0; i < Zquic::Path::ECNProbeThreshold - 1; ++i) {
    missing->sendECN(i, Zquic::EcnMark::ECT0);
    missing->ackNoECN(i);
  }
  diag = missing->runtimeDiag();
  ZuCHECK(!missing->ecnDisabled() && diag.tx.ecnValidationFailures == 0,
    "missing ACK_ECN failed before probe budget expired");
  missing->sendECN(
    Zquic::Path::ECNProbeThreshold - 1, Zquic::EcnMark::ECT0);
  missing->ackNoECN(Zquic::Path::ECNProbeThreshold - 1);
  diag = missing->runtimeDiag();
  ZuCHECK(missing->ecnDisabled() && diag.tx.ecnValidationFailures == 1,
    "missing ACK_ECN did not fail after probe budget expired");
  missing->closeForTest();

  ZmRef<TestLink> impossible = testLink(&app);
  impossible->enableECN();
  impossible->sendAckEliciting(0);
  diag = impossible->runtimeDiag();
  uint64_t inFlight = diag.tx.congestionBytesInFlight;
  unsigned queued = impossible->txFlushQueued;
  impossible->ackECN(0, 2, 0, 0);
  diag = impossible->runtimeDiag();
  ZuCHECK(impossible->ecnDisabled() &&
      diag.tx.ecnValidationFailures == 1 &&
      diag.tx.congestionBytesInFlight < inFlight &&
      impossible->txFlushQueued == queued + 1,
    "impossible ACK_ECN did not disable ECN while preserving ACK processing");
  impossible->closeForTest();

  ZmRef<TestLink> ce = testLink(&app);
  ce->enableECN();
  ce->sendECN(0, Zquic::EcnMark::ECT0);
  ce->sendECN(1, Zquic::EcnMark::ECT0);
  diag = ce->runtimeDiag();
  uint64_t cwnd = diag.tx.congestionWindow;
  ce->ackECN(1, 1, 0, 1);
  diag = ce->runtimeDiag();
  Zquic::PathDiag pathDiag = ce->pathDiag();
  ZuCHECK(!ce->ecnDisabled() &&
      diag.tx.peerAckECN[Zquic::PktNumSpace::AppData].ce == 1 &&
      diag.tx.congestionWindow < cwnd &&
      pathDiag.ecnCEEvents == 1 &&
      pathDiag.ecnCEBytes == 2400,
    "CE ACK_ECN did not drive congestion response and path diagnostics");
  ce->closeForTest();

	closeQLog_(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag qdiag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());

  ZuCHECK(qdiag.recordsEnqueued >= 3, "ECN qlog enqueue mismatch");
  ZuCHECK(qdiag.recordsWritten >= 4, "ECN qlog write mismatch");
  ZuCHECK(qdiag.writerFailures == 0, "ECN qlog writer failure");

  ZtString<> qlog = readFile_(path);
  ZuCHECK(qlog, "ECN qlog output missing");
  ZuCHECK(parseJSONSeq_(qlog) >= 4, "ECN qlog JSON-SEQ parse failed");
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
  removeTestLog_(path);
}
#endif

void testBlockedFrameDuplicateSuppression()
{
  ZuTestScope(testBlockedFrameDuplicateSuppression);

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
  auto stream = streamLink->stream(Zi::StreamType::Duplex);
  ZuCHECK(stream &&
      streamLink->queueStreamDataBlocked(uint64_t(stream->id()), 4096) &&
      !streamLink->queueStreamDataBlocked(uint64_t(stream->id()), 4096) &&
      streamLink->queuedControlFrames() == 1,
    "pending STREAM_DATA_BLOCKED duplicate was queued");
  (void)streamLink->flushControlSends();
  ZuCHECK(
      !streamLink->queuedControlFrames() &&
      !streamLink->queueStreamDataBlocked(uint64_t(stream->id()), 4096) &&
      streamLink->queueStreamDataBlocked(uint64_t(stream->id()), 8192) &&
      streamLink->queuedControlFrames() == 1,
    "sent STREAM_DATA_BLOCKED duplicate suppression mismatch");

  ZmRef<TestLink> limitLink = testLink(&app);
  limitLink->setPeerStreamLimit(Zi::StreamType::Duplex, 0);
  (void)limitLink->stream(Zi::StreamType::Duplex);
  ZuCHECK(limitLink->queuedControlFrames() == 1 &&
      !limitLink->queueStreamsBlocked(Zi::StreamType::Duplex, 0) &&
      limitLink->queuedControlFrames() == 1,
    "pending STREAMS_BLOCKED duplicate was queued");
  (void)limitLink->flushControlSends();
  ZuCHECK(
      !limitLink->queuedControlFrames() &&
      limitLink->queueStreamsBlocked(Zi::StreamType::Duplex, 0) &&
      limitLink->queuedControlFrames() == 1,
    "sent STREAMS_BLOCKED was not requeued");
  (void)limitLink->flushControlSends();
  ZuCHECK(
      !limitLink->queuedControlFrames() &&
      limitLink->queueStreamsBlocked(Zi::StreamType::Duplex, 8) &&
      limitLink->queuedControlFrames() == 1,
    "sent STREAMS_BLOCKED duplicate suppression mismatch");
}

void testKeyedControlReplacement()
{
  ZuTestScope(testKeyedControlReplacement);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  ZuCHECK(link->queueMaxData(1024) &&
      !link->queueMaxData(1024) &&
      link->queueMaxData(2048) &&
      link->queuedControlFrames() == 1,
    "MAX_DATA keyed replacement mismatch");

  ZmRef<TestLink> streams = testLink(&app);
  ZuCHECK(streams->queueStreamsBlocked(Zi::StreamType::Duplex, 7) &&
      !streams->queueStreamsBlocked(Zi::StreamType::Duplex, 7) &&
      streams->queueStreamsBlocked(Zi::StreamType::Duplex, 8) &&
      streams->queuedControlFrames() == 1,
    "STREAMS_BLOCKED keyed replacement mismatch");

  ZmRef<TestLink> path = testLink(&app);
  for (unsigned i = 0; i < TestLink::PathResponseMax + 4; ++i)
    ZuCHECK(path->queuePathResponse(i + 1), "PATH_RESPONSE queue failed");
  ZuCHECK(path->queuedControlFrames() ==
      TestLink::PathResponseMax,
    "PATH_RESPONSE FIFO exceeded retention cap");
}

void testControlInvalidation()
{
  ZuTestScope(testControlInvalidation);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  auto stream = link->stream(Zi::StreamType::Duplex);
  ZuCHECK(stream &&
      link->queueMaxStreamData(
	uint64_t(stream->id()), stream->rxCreditLimit()) &&
      link->queuedControlFrames() == 1,
    "MAX_STREAM_DATA control setup failed");
  uint8_t b[128];
  Zquic::Frame frame;
  int n = Zquic::FrameCodec::writeResetStream(
    b, sizeof(b), uint64_t(stream->id()), 7, 0);
  ZuCHECK(parseFrame_(b, n, frame) && link->receiveFrame(frame) == 0,
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
  auto packet = streamPkt_(0, 0, "req", true, frame, used);
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
  auto local = client->stream(Zi::StreamType::Duplex);
  packet = streamPkt_(local->id(), 0, "rsp", true, frame, used);
  ZuCHECK(packet,
    "response STREAM frame setup failed");
  ZuCHECK(client->receiveFrame(frame, packet, &diag) == 0 &&
      !client->streamedCount &&
      local->processed == 1 &&
      local->rxComplete(),
    "existing local bidi stream receive mismatch");

  packet = streamPkt_(1, 0, "bad", false, frame, used);
  ZuCHECK(packet,
    "local-origin STREAM setup failed");
  ZuCHECK(server->receiveFrame(frame, packet, &diag) < 0 && !server->findStream(1),
    "server accepted local-origin peer stream ID");

  ZmRef<TestLink> gap = testLink(&app, true);
  gap->setLocalStreamLimit(Zi::StreamType::Duplex, 3);
  packet = streamPkt_(8, 0, "hi", true, frame, used);
  ZuCHECK(packet && gap->receiveFrame(frame, packet) == 0 &&
      gap->peerStreamsOpened(Zi::StreamType::Duplex) == 3 &&
      gap->findStream(8),
    "higher peer stream was not accepted");
  packet = streamPkt_(4, 0, "lo", true, frame, used);
  ZuCHECK(packet && gap->receiveFrame(frame, packet) == 0,
    "implicit lower peer stream was rejected after higher stream");
  auto lower = gap->findStream(4);
  ZuCHECK(lower && lower->processed == 1 && lower->rxComplete() &&
      !gap->runtimeDiag().rx.closedStreamFrames &&
      !gap->runtimeDiag().rx.invalidStreamFrames,
    "implicit lower peer stream was treated as closed or invalid");
}

void testStreamCountLimits()
{
  ZuTestScope(testStreamCountLimits);

  App app;
  ZmRef<TestLink> client = testLink(&app);
  client->setPeerStreamLimit(Zi::StreamType::Duplex, 1);

  auto first = client->stream(Zi::StreamType::Duplex);
  ZuCHECK(first && first->id() == 0 &&
      client->localStreamsOpened(Zi::StreamType::Duplex) == 1 &&
      client->streamCount() == 1,
    "first stream under peer limit did not open");
  auto blocked = client->stream(Zi::StreamType::Duplex);
  ZuCHECK(!blocked &&
      client->localStreamsBlocked(Zi::StreamType::Duplex) &&
      client->queuedLocalStreams(Zi::StreamType::Duplex) == 1 &&
      client->streamCount() == 1,
    "stream over peer limit was not queued");

  uint8_t b[32];
  int n = Zquic::FrameCodec::writeMaxStreams(
    b, sizeof(b), Zi::StreamType::Duplex, 2);
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used),
    "MAX_STREAMS setup failed");
  ZuCHECK(client->applyMaxStreams(frame) &&
      !client->queuedLocalStreams(Zi::StreamType::Duplex) &&
      client->localStreamsOpened(Zi::StreamType::Duplex) == 2 &&
      client->streamCount() == 2 &&
      client->streamedCount == 1 &&
      client->lastStream &&
      client->lastStream->id() == 4,
    "MAX_STREAMS did not open queued local stream");

  ZmRef<TestLink> server = testLink(&app, true);
  server->setLocalStreamLimit(Zi::StreamType::Duplex, 1);
  auto packet = streamPkt_(0, 0, "a", true, frame, used);
  ZuCHECK(packet,
    "first peer STREAM setup failed");
  ZuCHECK(server->receiveFrame(frame, packet) == 0 &&
      server->peerStreamsOpened(Zi::StreamType::Duplex) == 1 &&
      server->findStream(0),
    "first peer stream under local limit did not open");

  packet = streamPkt_(4, 0, "b", true, frame, used);
  ZuCHECK(packet,
    "second peer STREAM setup failed");
  ZuCHECK(server->receiveFrame(frame, packet) < 0 &&
      server->peerStreamsOpened(Zi::StreamType::Duplex) == 1 &&
      !server->findStream(4),
    "peer stream count limit was not enforced");

  server->setLocalStreamLimit(Zi::StreamType::Duplex, 2);
  ZuCHECK(server->receiveFrame(frame, packet) == 0 &&
      server->peerStreamsOpened(Zi::StreamType::Duplex) == 2 &&
      server->findStream(4),
    "extended local stream count did not admit peer stream");

  server->setLocalStreamLimit(Zi::StreamType::Simplex, 1);
  packet = streamPkt_(2, 0, "u", true, frame, used);
  ZuCHECK(packet,
    "first peer uni STREAM setup failed");
  ZuCHECK(server->receiveFrame(frame, packet) == 0 &&
      server->peerStreamsOpened(Zi::StreamType::Simplex) == 1 &&
      server->findStream(2),
    "first peer uni stream under local limit did not open");

  packet = streamPkt_(6, 0, "v", true, frame, used);
  ZuCHECK(packet,
    "second peer uni STREAM setup failed");
  ZuCHECK(server->receiveFrame(frame, packet) == 0 &&
      server->peerStreamsOpened(Zi::StreamType::Simplex) == 2 &&
      server->localStreamLimit(Zi::StreamType::Simplex) == 3 &&
      server->findStream(6),
    "completed peer uni stream did not return stream-count credit");

  ZmRef<TestLink> bidiServer = testLink(&app, true);
  bidiServer->setLocalStreamLimit(Zi::StreamType::Duplex, 1);
  packet = streamPkt_(0, 0, "req", true, frame, used);
  ZuCHECK(packet && bidiServer->receiveFrame(frame, packet) == 0,
    "peer bidi stream receive setup failed");
  auto peerBidi = bidiServer->findStream(0);
  ZuCHECK(peerBidi && bidiServer->localStreamLimit(Zi::StreamType::Duplex) == 1,
    "peer bidi stream returned stream-count credit before local FIN");
  unsigned queuedControls = bidiServer->queuedControlFrames();
  unsigned queuedFlushes = bidiServer->txFlushQueued;
  bidiServer->initServerPath();
  bidiServer->pathReceived(1200);
  bidiServer->grantDataCredit(20000);
  peerBidi->txCredit(20000);
  {
    auto tx = peerBidi->txStream_();
    tx << "rsp" << Zi::flush();
  }
  peerBidi->fin();
  ZuCHECK(peerBidi->txRangeCount() == 1 &&
      peerBidi->txBufferedBytes() == 3 &&
      peerBidi->txCreditAvailable() >= 3 &&
      peerBidi->finSent(),
    "peer bidi response was not queued for transmit");
  ZuCHECK(bidiServer->flushCongestedStream(peerBidi),
    "peer bidi response flush failed");
  ZuCHECK(peerBidi->finDequeued(),
    "peer bidi response FIN was not dequeued");
  ZuCHECK(bidiServer->localStreamLimit(Zi::StreamType::Duplex) == 2,
    "completed peer bidi stream did not return stream-count credit");
  ZuCHECK(bidiServer->queuedControlFrames() == queuedControls + 1,
    "completed peer bidi stream did not queue MAX_STREAMS");
  ZuCHECK(bidiServer->txFlushQueued == queuedFlushes + 1,
    "completed peer bidi stream did not request MAX_STREAMS flush");
  bidiServer->cancelTimers();
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
      reset->error() == Zquic::StreamError::Reset &&
      reset->appError() == 7 &&
      reset->rxComplete() &&
      !reset->finalSize(),
    "RESET_STREAM state mismatch");

  ZmRef<TestLink> client = testLink(&app);
  auto local = client->stream(Zi::StreamType::Duplex);
  n = Zquic::FrameCodec::writeStopSending(b, sizeof(b), local->id(), 9);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used),
    "STOP_SENDING setup failed");
  ZuCHECK(client->receiveFrame(frame) == 0 &&
      local->stopReceived() &&
      local->error() == Zquic::StreamError::Stop &&
      local->appError() == 9,
    "STOP_SENDING state mismatch");

  auto delivered = client->stream(Zi::StreamType::Duplex);
  auto packet = streamPkt_(delivered->id(), 0, "hello", false, frame, used);
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

  auto reset = link->stream(Zi::StreamType::Duplex);
  {
    auto tx = reset->txStream_();
    tx << "abc" << Zi::flush();
  }
  reset->reset(42);
  ZuCHECK(reset->resetSent() &&
      reset->error() == Zquic::StreamError::Reset &&
      reset->appError() == 42 &&
      link->queuedControlFrames() == 1,
    "local RESET_STREAM did not queue control state");
  unsigned n = link->flushSentRefs(refs, 2);
  ZuCHECK(n == 1 &&
      refs[0].kind == Zquic::SentFrameKind::Control &&
      refs[0].controlType == Zquic::FrameType::ResetStream &&
      refs[0].streamID == uint64_t(reset->id()) &&
      refs[0].value == 42 &&
      refs[0].length == 3 &&
      !link->queuedControlFrames(),
    "local RESET_STREAM sent ref mismatch");
  Zquic::Frame rebuilt;
  ZuCHECK(link->rebuildControl(refs[0], rebuilt) &&
      rebuilt.type == Zquic::FrameType::ResetStream &&
      rebuilt.streamID == uint64_t(reset->id()) &&
      rebuilt.errorCode == 42 &&
      rebuilt.length == 3,
    "local RESET_STREAM retransmit rebuild failed");

  auto stop = link->stream(Zi::StreamType::Duplex);
  stop->stop(9);
  ZuCHECK(stop->stopSent() &&
      stop->error() == Zquic::StreamError::Stop &&
      stop->appError() == 9 &&
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
  link->cancelTimers();

  ZmRef<TestLink> lost = testLink(&app);
  auto lostStop = lost->stream(Zi::StreamType::Duplex);
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
  lost->cancelTimers();
}

void testMaxAndBlockedFrameValidation()
{
  ZuTestScope(testMaxAndBlockedFrameValidation);

  App app;
  uint8_t b[128];
  Zquic::Frame frame;

  ZmRef<TestLink> client = testLink(&app);
  auto localBidi = client->stream(Zi::StreamType::Duplex);
  auto localUni = client->stream(Zi::StreamType::Simplex);
  int n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), localBidi->id(), 4096);
  ZuCHECK(parseFrame_(b, n, frame) && client->applyMaxStreamData(frame) &&
      localBidi->txCreditLimit() == 4096,
    "MAX_STREAM_DATA did not extend local bidirectional stream credit");

  n = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 8, 4096);
  uint64_t invalidMax = client->runtimeDiag().rx.streamMaxInvalidRx;
  ZuCHECK(parseFrame_(b, n, frame) && client->applyMaxStreamData(frame) &&
      client->runtimeDiag().rx.streamMaxInvalidRx == invalidMax + 1 &&
      !client->findStream(8),
    "MAX_STREAM_DATA for uninitiated local stream was not diagnosed");

  n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), localUni->id(), 4096);
  ZuCHECK(parseFrame_(b, n, frame) && client->applyMaxStreamData(frame),
    "MAX_STREAM_DATA for local unidirectional sender was rejected");

  n = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 3, 4096);
  invalidMax = client->runtimeDiag().rx.streamMaxInvalidRx;
  ZuCHECK(parseFrame_(b, n, frame) && client->applyMaxStreamData(frame) &&
      client->runtimeDiag().rx.streamMaxInvalidRx == invalidMax + 1 &&
      !client->findStream(3),
    "MAX_STREAM_DATA for peer unidirectional stream was not diagnosed");

  ZmRef<TestLink> server = testLink(&app, true);
  server->setLocalStreamLimit(Zi::StreamType::Duplex, 2);
  n = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 0, 2048);
  ZuCHECK(parseFrame_(b, n, frame) && server->applyMaxStreamData(frame) &&
      server->findStream(0) &&
      server->peerStreamsOpened(Zi::StreamType::Duplex) == 1,
    "MAX_STREAM_DATA did not create valid peer bidirectional stream");

  ZmRef<TestLink> limited = testLink(&app, true);
  limited->setLocalStreamLimit(Zi::StreamType::Duplex, 1);
  n = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 4, 2048);
  invalidMax = limited->runtimeDiag().rx.streamMaxInvalidRx;
  ZuCHECK(parseFrame_(b, n, frame) && limited->applyMaxStreamData(frame) &&
      limited->runtimeDiag().rx.streamMaxInvalidRx == invalidMax + 1 &&
      !limited->findStream(4),
    "MAX_STREAM_DATA beyond local stream limit was not diagnosed");

  n = Zquic::FrameCodec::writeDataBlocked(
    b, sizeof(b), server->rxDataCreditLimit());
  ZuCHECK(parseFrame_(b, n, frame) && server->receiveDataBlocked(frame),
    "DATA_BLOCKED at local receive limit was rejected");
  n = Zquic::FrameCodec::writeDataBlocked(
    b, sizeof(b), server->rxDataCreditLimit() + 1);
  ZuCHECK(parseFrame_(b, n, frame) && !server->receiveDataBlocked(frame),
    "DATA_BLOCKED above local receive limit was accepted");

  n = Zquic::FrameCodec::writeStreamsBlocked(
    b, sizeof(b), Zi::StreamType::Duplex,
    server->localStreamLimit(Zi::StreamType::Duplex));
  ZuCHECK(parseFrame_(b, n, frame) && server->receiveStreamsBlocked(frame),
    "STREAMS_BLOCKED at local advertised peer limit was rejected");
  n = Zquic::FrameCodec::writeStreamsBlocked(
    b, sizeof(b), Zi::StreamType::Duplex,
    server->localStreamLimit(Zi::StreamType::Duplex) + 1);
  ZuCHECK(parseFrame_(b, n, frame) && !server->receiveStreamsBlocked(frame),
    "STREAMS_BLOCKED above local advertised peer limit was accepted");
}

void testStreamDataBlockedValidation()
{
  ZuTestScope(testStreamDataBlockedValidation);

  App app;
  uint8_t b[128];
  Zquic::Frame frame;

  ZmRef<TestLink> server = testLink(&app, true);
  server->setLocalStreamLimit(Zi::StreamType::Duplex, 2);
  int n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 0, 128);
  ZuCHECK(parseFrame_(b, n, frame) &&
      server->receiveStreamDataBlocked(frame) &&
      server->findStream(0) &&
      server->findStream(0)->rxCreditUsed() == 0 &&
      server->rxDataCreditUsed() == 0,
    "valid STREAM_DATA_BLOCKED mutated receive credit");

  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 0, 64);
  ZuCHECK(parseFrame_(b, n, frame) &&
      server->receiveStreamDataBlocked(frame) &&
      server->findStream(0)->rxCreditUsed() == 0 &&
      server->rxDataCreditUsed() == 0,
    "decreasing STREAM_DATA_BLOCKED mutated receive credit");

  n = Zquic::FrameCodec::writeStreamDataBlocked(
    b, sizeof(b), 0, server->findStream(0)->rxCreditLimit() + 1);
  ZuCHECK(parseFrame_(b, n, frame) &&
      !server->receiveStreamDataBlocked(frame),
    "STREAM_DATA_BLOCKED beyond stream receive limit was accepted");

  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 4, 128);
  ZuCHECK(parseFrame_(b, n, frame) &&
      server->receiveStreamDataBlocked(frame) &&
      server->findStream(4) &&
      server->peerStreamsOpened(Zi::StreamType::Duplex) == 2,
    "valid unopened peer STREAM_DATA_BLOCKED did not create stream");

  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 8, 128);
  ZuCHECK(parseFrame_(b, n, frame) &&
      !server->receiveStreamDataBlocked(frame) &&
      !server->findStream(8),
    "STREAM_DATA_BLOCKED opened stream beyond stream-count limit");

  ZmRef<TestLink> client = testLink(&app);
  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 2, 128);
  ZuCHECK(parseFrame_(b, n, frame) &&
      !client->receiveStreamDataBlocked(frame),
    "STREAM_DATA_BLOCKED for local unidirectional stream was accepted");

  ZmRef<TestLink> small = testLink(&app, true);
  small->setLocalStreamLimit(Zi::StreamType::Duplex, 1);
  n = Zquic::FrameCodec::writeStreamDataBlocked(
    b, sizeof(b), 0, small->rxDataCreditLimit() + 1);
  ZuCHECK(parseFrame_(b, n, frame) &&
      !small->receiveStreamDataBlocked(frame),
    "STREAM_DATA_BLOCKED beyond connection receive limit was accepted");
}

void testInvalidClosedStreamActivity()
{
  ZuTestScope(testInvalidClosedStreamActivity);

  App app;
  uint8_t b[128];
  Zquic::Frame frame;
  unsigned used = 0;

  ZmRef<TestLink> maxLink = testLink(&app);
  auto local = maxLink->stream(Zi::StreamType::Duplex);
  local->reset(1);
  int n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), uint64_t(local->id()), 4096);
  ZuCHECK(parseFrame_(b, n, frame) &&
      maxLink->applyMaxStreamData(frame) &&
      !maxLink->closeError(),
    "closed-stream MAX_STREAM_DATA was not ignored");
  Zquic::RuntimeDiag diag = maxLink->runtimeDiag();
  ZuCHECK(!diag.rx.invalidStreamFrames &&
      !diag.rx.closedStreamFrames &&
      diag.rx.streamMaxClosedRx == 1 &&
      !diag.rx.suspiciousStreamCloses,
    "closed-stream MAX_STREAM_DATA diagnostics mismatch");

  ZmRef<TestLink> resetLink = testLink(&app, true);
  n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 0);
  ZuCHECK(parseFrame_(b, n, frame) &&
      resetLink->receiveFrame(frame) == 0 &&
      resetLink->receiveFrame(frame) == 0,
    "duplicate RESET_STREAM final size was not ignored");
  diag = resetLink->runtimeDiag();
  ZuCHECK(!diag.rx.invalidStreamFrames &&
      !diag.rx.closedStreamFrames &&
      !resetLink->closeError(),
    "duplicate RESET_STREAM diagnostics mismatch");
  n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 1);
  ZuCHECK(parseFrame_(b, n, frame) &&
      resetLink->receiveFrame(frame) < 0 &&
      resetLink->closeError() == Zquic::TransportError::FinalSize,
    "active duplicate RESET_STREAM final-size violation did not close");
  diag = resetLink->runtimeDiag();
  ZuCHECK(diag.rx.suspiciousStreamCloses == 1,
    "final-size violation close diagnostics mismatch");

  ZmRef<TestLink> blockedLink = testLink(&app, true);
  n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 0);
  ZuCHECK(parseFrame_(b, n, frame) &&
      blockedLink->receiveFrame(frame) == 0,
    "closed STREAM_DATA_BLOCKED reset setup failed");
  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 0, 0);
  ZuCHECK(parseFrame_(b, n, frame) &&
      blockedLink->receiveStreamDataBlocked(frame) &&
      !blockedLink->closeError(),
    "closed-stream STREAM_DATA_BLOCKED was not ignored");
  diag = blockedLink->runtimeDiag();
  ZuCHECK(diag.rx.invalidStreamFrames == 1 &&
      diag.rx.closedStreamFrames == 1,
    "closed-stream STREAM_DATA_BLOCKED diagnostics mismatch");

  ZmRef<TestLink> dupLink = testLink(&app);
  auto dup = dupLink->stream(Zi::StreamType::Duplex);
  auto packet = streamPkt_(dup->id(), 0, "dup", false, frame, used);
  ZuCHECK(packet &&
      dupLink->receiveFrame(frame, packet) == 0,
    "duplicate STREAM setup failed");
  packet = streamPkt_(dup->id(), 0, "dup", false, frame, used);
  ZuCHECK(packet &&
      dupLink->receiveFrame(frame, packet) == 0 &&
      !dupLink->runtimeDiag().rx.invalidStreamFrames,
    "ordinary duplicate STREAM was treated as invalid");

  ZmRef<TestLink> threshold = testLink(&app);
  auto noisy = threshold->stream(Zi::StreamType::Duplex);
  noisy->reset(3);
  n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), uint64_t(noisy->id()), 4096);
  ZuCHECK(parseFrame_(b, n, frame), "threshold frame setup failed");
  for (unsigned i = 0; i < TestLink::suspiciousStreamThreshold(); ++i)
    ZuCHECK(threshold->applyMaxStreamData(frame),
      "closed-stream threshold frame was rejected");
  diag = threshold->runtimeDiag();
  ZuCHECK(!threshold->closeError() &&
      diag.rx.streamMaxClosedRx == TestLink::suspiciousStreamThreshold() &&
      !diag.rx.suspiciousStreamCloses,
    "closed-stream MAX_STREAM_DATA was not tracked as benign");
  ZuCHECK(threshold->applyMaxStreamData(frame) &&
      threshold->runtimeDiag().rx.streamMaxClosedRx ==
	TestLink::suspiciousStreamThreshold() + 1 &&
      !threshold->runtimeDiag().rx.suspiciousStreamCloses,
    "closed-stream MAX_STREAM_DATA produced suspicious close diagnostics");
}

void testStreamGC()
{
  ZuTestScope(testStreamGC);

  App app;

  ZmRef<TestLink> held = testLink(&app);
  auto live = held->stream(Zi::StreamType::Simplex);
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
  held->cancelTimers();

  ZmRef<TestLink> maxLink = testLink(&app);
  auto local = maxLink->stream(Zi::StreamType::Simplex);
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
  maxLink->cancelTimers();
  uint8_t b[128];
  Zquic::Frame frame;
  int n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), localID, 4096);
  ZuCHECK(parseFrame_(b, n, frame) &&
      maxLink->applyMaxStreamData(frame) &&
      maxLink->runtimeDiag().rx.streamMaxClosedRx == 1,
    "closed MAX_STREAM_DATA after stream GC was not compact-handled");

  ZmRef<TestLink> ackLink = testLink(&app);
  auto ackd = ackLink->stream(Zi::StreamType::Simplex);
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
  ackLink->cancelTimers();

  ZmRef<TestLink> rxLink = testLink(&app, true);
  unsigned used = 0;
  auto packet = streamPkt_(2, 0, "", true, frame, used);
  ZuCHECK(packet && rxLink->receiveFrame(frame, packet) == 0,
    "peer unidirectional stream GC setup failed");
  ZmRef<TestStream> rx = rxLink->lastStream;
  ZuCHECK(rx && rx->rxComplete(),
    "peer unidirectional stream did not reach terminal receive state");
  ZuCHECK(!rxLink->findStream(2),
    "terminal FIN did not remove peer stream from active index");
  rxLink->lastStream = nullptr;
  rx = nullptr;
  packet = streamPkt_(2, 0, "", true, frame, used);
  ZuCHECK(packet && rxLink->receiveFrame(frame, packet) == 0 &&
      !rxLink->runtimeDiag().rx.closedStreamFrames,
    "duplicate STREAM after stream GC was not compact-handled");
  packet = streamPkt_(2, 0, "long", true, frame, used);
  ZuCHECK(packet && rxLink->receiveFrame(frame, packet) == 0 &&
      !rxLink->closeError(),
    "late STREAM after stream GC was not classified as closed");
}

void testFrameRoleAndSpaceLegality()
{
  ZuTestScope(testFrameRoleAndSpaceLegality);

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
  ZuCHECK(Zquic::PktProt::deriveTrafficSecret(
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
  ZuCHECK(link->runtimeDiag().tx.packetsTx == 1,
    "0-RTT send did not record AppData packet accounting");
  link->cancelTimers();
}

void testZeroRTTEarlyStreamPolicy()
{
  ZuTestScope(testZeroRTTEarlyStreamPolicy);

  uint8_t secretBytes[32];
  for (unsigned i = 0; i < sizeof(secretBytes); ++i)
    secretBytes[i] = uint8_t(0xa0 + i);
  Zquic::TrafficSecret secret;
  ZuCHECK(Zquic::PktProt::deriveTrafficSecret(
      secret, &ptls_openssl_aes128gcmsha256,
      span_(secretBytes, sizeof(secretBytes))),
    "0-RTT traffic secret derivation failed");

  App app;
  ZmRef<TestLink> link = testLink(&app);
  ZuCHECK(link->installZeroRTTTx(secret),
    "0-RTT Tx key install failed");
  link->grantDataCredit(20000);
  auto stream = link->stream(Zi::StreamType::Duplex);
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
  link->cancelTimers();
}

#ifdef Zquic_DEBUG
void testZeroRTTAcceptQLog()
{
  ZuTestScope(testZeroRTTAcceptQLog);

  Zi::Path path = testPath_("ZquicStreamZeroRTTAcceptQLog.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).ringSize(1<<15);

  App app;
  ZuCHECK(ZquicLogger::init(
      app.qlogTrace(), params, Zquic::Vantage::Unknown),
    "0-RTT accept qlog init failed");
  ZquicLogger::start();

  Zquic::TrafficSecret secret;
  ZuCHECK(trafficSecret_(secret, 0xd0),
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

  closeQLog_(app.qlogTrace());
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(app.qlogTrace());
  client->cancelTimers();
  server->cancelTimers();

  ZuCHECK(diag.recordsEnqueued >= 3, "0-RTT accept qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 4, "0-RTT accept qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "0-RTT accept qlog writer failure");

  ZtString<> qlog = readFile_(path);
  ZuCHECK(qlog, "0-RTT accept qlog output missing");
  ZuCHECK(parseJSONSeq_(qlog) >= 4,
    "0-RTT accept qlog JSON-SEQ parse failed");
  ZuCHECK(qlog.find<"zquic:zero_rtt_accepted">() >= 0 &&
      qlog.find<"\"reason\":\"0rtt\"">() >= 0 &&
      qlog.find<"\"success\":true">() >= 0,
    "0-RTT accept qlog event missing");
  removeTestLog_(path);
}

void testZeroRTTAfterOneRTTRejected()
{
  ZuTestScope(testZeroRTTAfterOneRTTRejected);

  Zi::Path path = testPath_("ZquicStreamZeroRTTRejectQLog.sqlog");
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
  ZuCHECK(trafficSecret_(rx, 0xb0) && trafficSecret_(tx, 0xc0),
    "1-RTT traffic secret derivation failed");

  ZmRef<TestLink> link = testLink(&app);
  Zquic::CxnID dcid{"server01"};
  ZuCHECK(link->installOneRTT(rx, tx, dcid),
    "1-RTT key install failed");
  ZuCHECK(link->receiveShort(shortPing_(dcid, rx, 0, false)) &&
      link->rxOneRTTSeen(),
    "1-RTT receive did not advance AppData receive state");
  uint64_t packets = link->runtimeDiag().rx.packetsRx;

  ZuCHECK(link->receiveZeroRTT(zeroRTTLongHdr_(1)),
    "late 0-RTT packet was treated as a receive failure");
  ZuCHECK(link->rxEarlyState() == Zquic::LinkEarlyState::Rejected &&
      link->txEarlyState() == Zquic::LinkEarlyState::Rejected &&
      link->runtimeDiag().rx.packetsRx == packets,
    "late 0-RTT was not rejected before decrypt/accounting");

  closeQLog_(app.qlogTrace());
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(app.qlogTrace());
  link->cancelTimers();

  ZuCHECK(diag.recordsEnqueued >= 3, "0-RTT reject qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 4, "0-RTT reject qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "0-RTT reject qlog writer failure");

  ZtString<> qlog = readFile_(path);
  ZuCHECK(qlog, "0-RTT reject qlog output missing");
  ZuCHECK(parseJSONSeq_(qlog) >= 4,
    "0-RTT reject qlog JSON-SEQ parse failed");
  ZuCHECK(qlog.find<"zquic:zero_rtt_rejected">() >= 0 &&
      qlog.find<"\"reason\":\"0rtt_after_1rtt\"">() >= 0 &&
      qlog.find<"\"success\":false">() >= 0,
    "0-RTT reject qlog event missing");
  ZuCHECK(qlog.find<"quic:packet_dropped">() >= 0 &&
      qlog.find<"0rtt_after_1rtt">() >= 0,
    "late 0-RTT packet drop qlog event missing");
  removeTestLog_(path);
}

void testRuntimeReceiveQLog()
{
  ZuTestScope(testRuntimeReceiveQLog);

  Zi::Path path = testPath_("ZquicStreamRuntimeQLog.sqlog");
  ZiFile::remove(path);

  App app;
  ZmRef<TestLink> link = testLink(&app);
  Zquic::CxnID cid{"qlogcid1"};
  Zquic::TrafficSecret secret;
  ZuCHECK(trafficSecret_(secret, 11), "1-RTT secret derivation failed");
  ZuCHECK(link->installOneRTT(secret, secret, cid),
    "test 1-RTT secret install failed");

  bool disabledSeen = true;
  ZuCHECK(link->receiveShort(shortPing_(cid, secret, 1, false), &disabledSeen),
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
  ZuCHECK(link->receiveShort(shortPing_(cid, secret, 2, false), &enabledSeen),
    "enabled-qlog receive packet was rejected");
  ZuCHECK(enabledSeen, "enabled qlog did not construct receive accumulator");

  ZmRef<ZiIOBuf> bad = shortPing_(cid, secret, 3, false);
  ZuCHECK(bad && bad->length, "runtime qlog corrupt packet setup failed");
  bad->data_()[bad->length - 1] ^= 0x01;
  ZuCHECK(!link->receiveShort(ZuMv(bad)),
    "runtime qlog corrupt packet was accepted");

	closeQLog_(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());
  link->cancelTimers();

  ZuCHECK(diag.recordsEnqueued >= 2, "runtime qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 4, "runtime qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "runtime qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "runtime qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 2, "runtime qlog JSON-SEQ parse failed");
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

  Zi::Path path = testPath_("ZquicStreamFlowControlQLog.sqlog");
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
  ZmRef<TestStream> stream = link->stream(Zi::StreamType::Duplex);
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

  ZmRef<TestStream> streamBlocked = link->stream(Zi::StreamType::Duplex);
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

	closeQLog_(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());
  link->cancelTimers();

  ZuCHECK(diag.recordsEnqueued >= 4, "flow-control qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 5, "flow-control qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "flow-control qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "flow-control qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 5, "flow-control qlog JSON-SEQ parse failed");
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

  Zi::Path path = testPath_("ZquicStreamPTOQLog.sqlog");
  ZiFile::remove(path);

	ZquicLogParams params;
	params.enabled(true).path(path).ringSize(1<<15);

	App app;
	ZuCHECK(ZquicLogger::init(
	    app.qlogTrace(), params, Zquic::Vantage::Unknown),
	  "PTO qlog init failed");
	ZquicLogger::start();
	ZmRef<TestLink> link = testLink(&app);
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
  link->cancelTimers();

	closeQLog_(app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(app.qlogTrace());

  ZuCHECK(diag.recordsEnqueued >= 3, "PTO qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 4, "PTO qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "PTO qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "PTO qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 4, "PTO qlog JSON-SEQ parse failed");
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
  removeTestLog_(path);
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
  ZuCHECK(trafficSecret_(initial, 7), "initial 1-RTT secret derivation failed");
  ZuCHECK(Zquic::PktProt::deriveNextTrafficSecret(next, initial),
    "next 1-RTT secret derivation failed");
  ZuCHECK(Zquic::PktProt::deriveNextTrafficSecret(third, next),
    "third 1-RTT secret derivation failed");
  ZuCHECK(link->installOneRTT(initial, initial, cid),
    "test 1-RTT secret install failed");

  bool currentOK = link->receiveShort(shortPing_(cid, initial, 1, false));
  bool updateOK = link->receiveShort(shortPing_(cid, next, 2, true));
  Zquic::RuntimeDiag updateDiag = link->runtimeDiag();
  bool oldOK = link->receiveShort(shortPing_(cid, initial, 0, false));
  Zquic::RuntimeDiag oldDiag = link->runtimeDiag();
  bool rapidOK = link->receiveShort(shortPing_(cid, third, 3, false));
  Zquic::RuntimeDiag invalidDiag = link->runtimeDiag();
  link->discardPeerKeys();
  Zquic::RuntimeDiag discardDiag = link->runtimeDiag();
  bool discardedOK = link->receiveShort(shortPing_(cid, initial, 4, false));
  link->cancelTimers();

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
  ZuTestMain();
  ZuTestCall(testStreamIDs);
  ZuTestCall(testStreamFrameDelivery);
  ZuTestCall(testStreamRxSliceDelivery);
  ZuTestCall(testOutOfOrderStreamDelivery);
  ZuTestCall(testStreamTxRetention);
  ZuTestCall(testStreamTxUnackd);
  ZuTestCall(testStreamTxUnackdFin);
  ZuTestCall(testLinkStreamTxUnackdAck);
  ZuTestCall(testLinkStreamRetransmitUnackdIdempotent);
  ZuTestCall(testStreamPktizer);
  ZuTestCall(testQueuedControlSendFailureRetainsFrame);
  ZuTestCall(testRuntimeMultiFrameAssembly);
  ZuTestCall(testStreamRetransmitClipsUnackd);
  ZuTestCall(testStreamRetransmitClipsUnackdFin);
  ZuTestCall(testCryptoRetransmitClipsUnackd);
  ZuTestCall(testRuntimePacketNumberLength);
  ZuTestCall(testLongHeaderCoalescing);
  ZuTestCall(testCongestionBudgetGatesRuntimeSends);
  ZuTestCall(testActivePathRuntimeBudget);
  ZuTestCall(testPathValidationStateMachine);
#ifdef Zquic_DEBUG
  ZuTestCall(testPMTUDQLog);
  ZuTestCall(testCIDQLog);
  ZuTestCall(testAckECNValidationDisablesECN);
  ZuTestCall(testRuntimeReceiveQLog);
  ZuTestCall(testFlowControlQLog);
  ZuTestCall(testPTOQLog);
#endif
  ZuTestCall(testBlockedFrameDuplicateSuppression);
  ZuTestCall(testKeyedControlReplacement);
  ZuTestCall(testControlInvalidation);
  ZuTestCall(testPeerStreamAcceptance);
  ZuTestCall(testStreamCountLimits);
  ZuTestCall(testResetStopFrames);
  ZuTestCall(testLocalResetStopSend);
  ZuTestCall(testMaxAndBlockedFrameValidation);
  ZuTestCall(testStreamDataBlockedValidation);
  ZuTestCall(testInvalidClosedStreamActivity);
  ZuTestCall(testStreamGC);
  ZuTestCall(testFrameRoleAndSpaceLegality);
  ZuTestCall(testZeroRTTProtectedSend);
  ZuTestCall(testZeroRTTEarlyStreamPolicy);
#ifdef Zquic_DEBUG
  ZuTestCall(testZeroRTTAcceptQLog);
  ZuTestCall(testZeroRTTAfterOneRTTRejected);
  ZuTestCall(testPeerKeyUpdateState);
#endif
}
