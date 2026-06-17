//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>
#include <zlib/ZquicSched.hh>

using namespace ZuTestUtil;

using StreamTxBufAlloc = Zquic::StreamTxBufAlloc<>;

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
struct TestLink :
  public Zquic::Link<App, TestLink,
    StreamTxBufAlloc, TestStream>
{
  using Base = Zquic::Link<App, TestLink,
    StreamTxBufAlloc, TestStream>;
  TestLink(App *app, bool isServer = false) : Base{app, isServer} {
    Base::resetRuntime_();
    Base::configureLocalTransportParams_(app);
  }
  void streamed(ZmRef<TestStream> stream) {
    lastStream = ZuMv(stream);
    ++streamedCount;
  }
  bool queuePathResponse() {
    static const char data[] = "response";
    return Base::queuePathResponse_(ZuCSpan{data, sizeof(data) - 1});
  }
  bool queuePathResponse(unsigned seed) {
    uint8_t data[8]{};
    data[0] = uint8_t(seed);
    return Base::queuePathResponse_(ZuCSpan{data, sizeof(data)});
  }
  bool queueDataBlocked(uint64_t value) {
    return Base::queueBlocked_(Zquic::FrameType::DataBlocked, 0, value);
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
      [&](Zquic::PktBuild &, ZiSockAddr, const typename Base::TxPktRefs &r) {
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
      [&](Zquic::PktBuild &, ZiSockAddr, const typename Base::TxPktRefs &r) {
	n = r.count();
	for (unsigned i = 0; i < n && i < capacity; ++i) refs[i] = r[i];
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
      ZuCSpan{
	reinterpret_cast<const char *>(build.data()[0].base),
	unsigned(build.data()[0].len)},
      frame, used) && used == build.data()[0].len;
  }
  bool flushCongestedStream(ZmRef<TestStream> stream) {
    return Base::sendQueuedStreamPkt_(
      ZuMv(stream), ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [this](
	  Zquic::PktBuild &build, ZiSockAddr,
	  const Zquic::SentFrameRef &ref) {
	Base::recordTxPkt_(
	  Zquic::CryptoLevel::OneRTT, sentPkts, build.bytes(), ref, true);
	++sentPkts;
	return true;
      });
  }
  void fillCwnd() {
    Zquic::RuntimeDiag diag = Base::runtimeDiag_();
    while (diag.congestionBytesInFlight < diag.congestionWindow) {
      uint64_t remaining =
	diag.congestionWindow - diag.congestionBytesInFlight;
      unsigned bytes = remaining > app()->maxUDP() ?
	app()->maxUDP() : unsigned(remaining);
      Base::recordTxPkt_(
	Zquic::CryptoLevel::OneRTT, sentPkts, bytes,
	Zquic::SentFrameRef::control(), true);
      ++sentPkts;
      diag = Base::runtimeDiag_();
    }
  }
  void ackThrough(uint64_t pn) {
    Base::AckSnapshot ack;
    ack.level = Zquic::CryptoLevel::OneRTT;
    ack.nRanges = 1;
    ack.ranges[0] = Zquic::AckRange{pn, 0};
    Base::processAckFrameTx_(ack);
  }
  void advancePN(unsigned n) {
    for (unsigned i = 0; i < n; ++i)
      Base::recordProtPktTx_(
	Zquic::CryptoLevel::OneRTT, i, 1, {}, nullptr, false);
  }
  void forcePN(uint64_t pn) {
    Base::setTxPNForTest_(Zquic::CryptoLevel::OneRTT, pn);
  }
  unsigned pnLength() const {
    return Base::txPNLength_(Zquic::CryptoLevel::OneRTT);
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
      [&](auto buf, ZiSockAddr) {
	++sends;
	bytes += buf->length;
	return true;
      });
    ok = Base::sendHandshakeCoalesced_(
      ZuMv(handshake), ZiSockAddr{},
      [&](auto buf, ZiSockAddr) {
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
    Base::recordTxPkt_(
      Zquic::CryptoLevel::OneRTT, pn, bytes,
      Zquic::SentFrameRef::control(), true);
  }
  void ackECN(uint64_t largest, uint64_t ect0, uint64_t ect1, uint64_t ce) {
    Base::AckSnapshot ack;
    ack.level = Zquic::CryptoLevel::OneRTT;
    ack.nRanges = 1;
    ack.ranges[0] = Zquic::AckRange{largest, 0};
    ack.ecn.ect0 = ect0;
    ack.ecn.ect1 = ect1;
    ack.ecn.ce = ce;
    Base::processAckFrameTx_(ack);
  }
  bool receiveMarked(uint64_t pn, Zquic::EcnMark::T ecn) {
    bool ok = Base::recordRxPkt_(Zquic::CryptoLevel::OneRTT, pn, ecn);
    if (ok) Base::postAckSnapshot_(Zquic::CryptoLevel::OneRTT, ZiSockAddr{});
    return ok;
  }
  bool writePendingAck(Zquic::PktBuild &build) {
    return Base::appendPendingAck_(Zquic::CryptoLevel::OneRTT, build);
  }
  Zquic::PktBudget sendBudget() const { return Base::sendBudget_(); }
  void initServerPath() {
    Base::initServerPathTx_(ZiSockAddr{}, ZiSockAddr{});
  }
  void initServerPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initServerPathTx_(ZuMv(local), ZuMv(remote));
  }
  void observePath(ZiSockAddr local, ZiSockAddr remote) {
    Base::startPathValidationForTest_(ZuMv(local), ZuMv(remote));
  }
  bool validatingPath() const { return Base::validatingPath_(); }
  ZuCSpan validatingChallenge() const {
    return Base::validatingChallenge_();
  }
  bool pathResponse(ZuCSpan data) { return Base::onPathResponse_(data); }
  void pathTimeout() { Base::pathExpired_(); }
  const ZiSockAddr &activePathRemote() const {
    return Base::activePathRemote_();
  }
  void pathReceived(unsigned bytes) { Base::recordPathRxTx_(bytes); }
  bool pathSend(unsigned bytes) {
    ZmRef<ZiIOBuf> buf = new Zquic::PktTxBufAlloc<>{nullptr};
    buf->skip = 0;
    buf->length = bytes;
    return Base::sendPathPkt_(
      ZuMv(buf), ZiSockAddr{},
      [](ZmRef<ZiIOBuf>, ZiSockAddr) { return true; });
  }
  void validatePath() { Base::validatePathTx_(); }
  void growActivePath(unsigned size) { Base::growActivePathForTest_(size); }
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

static bool parseFrame_(const uint8_t *b, int n, Zquic::Frame &frame)
{
  unsigned used = 0;
  return n > 0 && !Zquic::FrameCodec::parse(
    ZuCSpan{b, unsigned(n)}, frame, used) &&
    used == unsigned(n);
}

static ZmRef<ZiIOBuf> streamPkt_(
  uint64_t id, uint64_t offset, ZuCSpan payload, bool fin,
  Zquic::Frame &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = new Zquic::PktRxBufAlloc<>{nullptr};
  int n = Zquic::FrameCodec::writeStream(
    packet->data_(), packet->size, id, offset, payload, fin);
  if (n <= 0) return nullptr;
  packet->skip = 0;
  packet->length = unsigned(n);
  if (Zquic::FrameCodec::parse(packet->cspan(), frame, used) ||
      used != packet->length)
    return nullptr;
  return packet;
}

static bool consumeExact_(Zquic::RxStream &rx, unsigned n, ZuCSpan expected)
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
    [&](ZuBSpan span) {
      called = true;
      ok = span.length() == expected.length() &&
	!memcmp(span.data(), expected.data(), expected.length());
    });
  return consumed == n && called && ok;
}

static bool sameAddr_(const ZiSockAddr &l, const ZiSockAddr &r)
{
  if (!l || !r) return !l && !r;
  return l.m_sin.sin_family == r.m_sin.sin_family &&
    l.m_sin.sin_port == r.m_sin.sin_port &&
    l.m_sin.sin_addr.s_addr == r.m_sin.sin_addr.s_addr;
}

void testStreamIDs()
{
  ZuTestScope(testStreamIDs);

  App app;
  TestLink client{&app};
  auto c0 = client.stream(Zi::StreamType::Duplex);
  auto c1 = client.stream(Zi::StreamType::Simplex);
  ZuCHECK(c0->id() == 0 && c1->id() == 2, "client stream IDs mismatch");

  TestLink server{&app, true};
  auto s0 = server.stream(Zi::StreamType::Duplex);
  auto s1 = server.stream(Zi::StreamType::Simplex);
  ZuCHECK(s0->id() == 1 && s1->id() == 3, "server stream IDs mismatch");
  ZuCHECK(server.findStream(1) == s0, "stream table lookup failed");
}

void testStreamFrameDelivery()
{
  ZuTestScope(testStreamFrameDelivery);

  App app;
  TestLink client{&app};
  auto stream = client.stream(Zi::StreamType::Duplex);

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
  TestLink client{&app};
  auto stream = client.stream(Zi::StreamType::Duplex);

  ZmRef<ZiIOBuf> packet = new Zquic::PktRxBufAlloc<>{nullptr};
  int n = Zquic::FrameCodec::writeStream(
    packet->data_(), packet->size, stream->id(), 0, "slice", false);
  ZuCHECK(n > 0, "packet-backed STREAM frame write failed");
  packet->skip = 0;
  packet->length = unsigned(n);

  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zquic::FrameCodec::parse(packet->cspan(), frame, used) &&
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
  TestLink client{&app};
  auto stream = client.stream(Zi::StreamType::Duplex);

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

  auto split = client.stream(Zi::StreamType::Duplex);
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
  TestLink client{&app};
  auto stream = client.stream(Zi::StreamType::Duplex);

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

void testStreamPktizer()
{
  ZuTestScope(testStreamPktizer);

  uint8_t prefix[32];
  uint8_t assembled[64];
  ZuCSpan prefixPayload{"prefix-payload", 14};
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
      ZuCSpan{assembled, unsigned(prefixLen) + prefixPayload.length()},
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
  TestLink client{&app};
  auto stream = client.stream(Zi::StreamType::Duplex);
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
      ZuCSpan{assembled, info.bytes},
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
      ZuCSpan{assembled, info.bytes},
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

  auto blocked = client.stream(Zi::StreamType::Duplex);
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

  auto split = client.stream(Zi::StreamType::Duplex);
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
      ZuCSpan{assembled, info.bytes},
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
  TestLink client{&app};
  ZuCHECK(client.queuePathResponse() &&
      client.queuedControlFrames() == 1,
    "queued control setup failed");
  ZuCHECK(!client.flushControlSendFails() &&
      client.queuedControlFrames() == 1,
    "failed control send dropped queued frame");
  ZuCHECK(client.flushControlSends() &&
      !client.queuedControlFrames(),
    "successful control send did not dequeue frame");
}

void testRuntimeMultiFrameAssembly()
{
  ZuTestScope(testRuntimeMultiFrameAssembly);

  App app;
  TestLink controls{&app};
  ZuCHECK(
      controls.queuePathResponse(1) &&
      controls.queuePathResponse(2) &&
      controls.queuePathResponse(3),
    "multi-control setup failed");
  Zquic::SentFrameKind::T kinds[8]{};
  uint64_t streamIDs[8]{};
  unsigned refs = controls.flushFrameRefs(kinds, streamIDs, 8);
  ZuCHECK(refs == 3 &&
      kinds[0] == Zquic::SentFrameKind::Control &&
      kinds[1] == Zquic::SentFrameKind::Control &&
      kinds[2] == Zquic::SentFrameKind::Control &&
      !controls.queuedControlFrames(),
    "runtime did not assemble multiple control frames into one packet");

  TestLink streams{&app};
  auto s0 = streams.stream(Zi::StreamType::Duplex);
  auto s1 = streams.stream(Zi::StreamType::Duplex);
  streams.grantDataCredit(20000);
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
  if (!streams.scheduledStreams()) {
    streams.scheduleStream(s0);
    streams.scheduleStream(s1);
  }
  refs = streams.flushFrameRefs(kinds, streamIDs, 8);
  ZuCHECK(refs == 2 &&
      kinds[0] == Zquic::SentFrameKind::Stream &&
      kinds[1] == Zquic::SentFrameKind::Stream &&
      streamIDs[0] == uint64_t(s0->id()) &&
      streamIDs[1] == uint64_t(s1->id()),
    "runtime did not assemble multiple stream frames into one packet");

  TestLink finOnly{&app};
  auto finStream = finOnly.stream(Zi::StreamType::Duplex);
  finOnly.grantDataCredit(20000);
  finStream->txCredit(20000);
  {
    auto tx = finStream->txStream_();
    tx << "payload" << Zi::flush();
  }
  Zquic::SentFrameRef sentRefs[8];
  refs = finOnly.flushSentRefs(sentRefs, 8);
  ZuCHECK(refs == 1 && sentRefs[0].kind == Zquic::SentFrameKind::Stream &&
      sentRefs[0].length == 7 && !sentRefs[0].fin,
    "runtime did not send first stream data frame");
  finStream->fin();
  refs = finOnly.flushSentRefs(sentRefs, 8);
  ZuCHECK(refs == 1 && sentRefs[0].kind == Zquic::SentFrameKind::Stream &&
      sentRefs[0].streamID == uint64_t(finStream->id()) &&
      sentRefs[0].offset == 7 && !sentRefs[0].length &&
      sentRefs[0].fin,
    "runtime lost FIN-only stream offset");
}

void testRuntimePacketNumberLength()
{
  ZuTestScope(testRuntimePacketNumberLength);

  App app;
  TestLink link{&app};
  ZuCHECK(link.pnLength() == 2,
    "runtime PN length default before ACK changed");
  link.advancePN(10);
  link.ackThrough(9);
  ZuCHECK(link.pnLength() == 1,
    "runtime PN length did not shrink after near ACK");
  link.advancePN(200);
  ZuCHECK(link.pnLength() == 2,
    "runtime PN length did not grow to two bytes");
  link.forcePN(40000);
  ZuCHECK(link.pnLength() == 3,
    "runtime PN length did not grow to three bytes");
  link.forcePN(9000000);
  ZuCHECK(link.pnLength() == 4,
    "runtime PN length did not grow to four bytes");
  link.close();
}

void testLongHeaderCoalescing()
{
  ZuTestScope(testLongHeaderCoalescing);

  App app;
  TestLink link{&app};
  unsigned sends = 0;
  unsigned bytes = 0;
  ZuCHECK(link.coalesceProbe(sends, bytes) &&
      sends == 1 &&
      bytes == 140,
    "Initial/Handshake coalescing did not emit one combined datagram");
  link.close();
}

void testCongestionBudgetGatesRuntimeSends()
{
  ZuTestScope(testCongestionBudgetGatesRuntimeSends);

  App app;
  TestLink link{&app};
  auto stream = link.stream(Zi::StreamType::Duplex);
  stream->txCredit(20000);
  {
    auto tx = stream->txStream_();
    tx << "0123456789abcdef0123456789abcdef" << Zi::flush();
  }
  stream->fin();

  link.fillCwnd();
  Zquic::RuntimeDiag diag = link.runtimeDiag();
  ZuCHECK(diag.congestionBytesInFlight >= diag.congestionWindow &&
      !link.flushCongestedStream(stream) &&
      stream->txBufferedBytes(),
    "runtime stream send bypassed closed congestion window");

  ZuCHECK(!link.congestionAllowance(),
    "runtime congestion allowance remained open after cwnd fill");
  ZuCHECK(link.queuePathResponse(), "runtime control frame queue failed");
  ZuCHECK(!link.flushControlSends(),
    "runtime control send bypassed closed congestion window");
  ZuCHECK(link.queuedControlFrames() == 1,
    "runtime blocked control frame was not retained");

  unsigned queued = link.txFlushQueued;
  link.ackThrough(link.sentPkts - 1);
  diag = link.runtimeDiag();
  ZuCHECK(diag.congestionBytesInFlight < diag.congestionWindow &&
      link.txFlushQueued == queued + 1,
    "runtime ACK did not reopen congestion window and queue Tx flush");
  bool flushed = link.flushControlSends();
  ZuCHECK(flushed, "runtime control send did not resume after ACK opened cwnd");
  ZuCHECK(!link.queuedControlFrames(),
    "runtime control queue did not drain after ACK opened cwnd");
  link.close();
}

void testActivePathRuntimeBudget()
{
  ZuTestScope(testActivePathRuntimeBudget);

  {
    App app{1360};
    TestLink client{&app};
    client.growActivePath(1360);
    Zquic::PktBudget budget = client.sendBudget();
    ZuCHECK(client.pathValidated() &&
	client.activePathMaxUDP() == 1360 &&
	budget.pmtu == 1360 &&
	budget.limit() == 1360,
      "active path max UDP did not drive runtime packet budget");
    client.close();
  }

  App app;
  TestLink server{&app, true};
  server.initServerPath();
  ZuCHECK(!server.pathValidated() &&
      !server.pathAntiAmplification() &&
      !server.pathSend(1),
    "unvalidated server path sent without received bytes");

  server.pathReceived(400);
  ZuCHECK(server.pathAntiAmplification() == 1200 &&
      server.pathSend(1000) &&
      !server.pathSend(201) &&
      server.pathSend(200) &&
      !server.pathAntiAmplification(),
    "unvalidated server path did not enforce 3x send budget");

  Zquic::PathDiag diag = server.pathDiag();
  ZuCHECK(diag.bytesRx == 400 && diag.bytesTx == 1200,
    "active path byte accounting mismatch after budget exhaustion");

  server.pathReceived(100);
  ZuCHECK(server.pathAntiAmplification() == 300 &&
      server.pathSend(300),
    "received bytes did not expand server anti-amplification budget");

  server.validatePath();
  ZuCHECK(server.pathValidated() &&
      server.pathAntiAmplification() == uint64_t(-1) &&
      server.pathSend(Zquic::MinUDPPayload),
    "address validation did not unlock active path send allowance");
  server.close();
}

void testPathValidationStateMachine()
{
  ZuTestScope(testPathValidationStateMachine);

  Zquic::PathChallenge challenge;
  ZuCHECK(challenge.generate() &&
      challenge.valid() &&
      challenge.length() == Zquic::PathChallenge::Length &&
      challenge.equals(challenge.cspan()),
    "PATH_CHALLENGE value generation failed");

  App app;
  TestLink link{&app, true};
  ZiSockAddr local{ZiIP{0x0a000001}, 4433};
  ZiSockAddr oldRemote{ZiIP{0x0a000002}, 50000};
  ZiSockAddr newRemote{ZiIP{0x0a000002}, 50001};
  ZiSockAddr otherRemote{ZiIP{0x0a000002}, 50002};

  link.initServerPath(local, oldRemote);
  link.validatePath();
  ZuCHECK(sameAddr_(link.activePathRemote(), oldRemote),
    "active path remote setup failed");

  link.observePath(local, newRemote);
  ZuCSpan data = link.validatingChallenge();
  uint8_t response[Zquic::PathChallenge::Length]{};
  memcpy(response, data.data(), data.length());
  ZuCHECK(link.validatingPath() &&
      data.length() == Zquic::PathChallenge::Length &&
      link.queuedControlFrames() == 1 &&
      sameAddr_(link.activePathRemote(), oldRemote),
    "new path replaced active remote before validation");

  uint8_t bad[Zquic::PathChallenge::Length]{};
  bad[0] = response[0] ^ 0xffU;
  ZuCHECK(!link.pathResponse(ZuCSpan{bad, sizeof(bad)}) &&
      link.validatingPath() &&
      sameAddr_(link.activePathRemote(), oldRemote),
    "unmatched PATH_RESPONSE changed path-validation state");

  link.observePath(local, newRemote);
  ZuCHECK(link.queuedControlFrames() == 1,
    "repeated packet from validating path queued duplicate challenge");

  ZuCHECK(link.pathResponse(ZuCSpan{response, sizeof(response)}) &&
      !link.validatingPath() &&
      link.pathValidated() &&
      sameAddr_(link.activePathRemote(), newRemote),
    "matching PATH_RESPONSE did not promote candidate path");

  link.observePath(local, otherRemote);
  ZuCHECK(link.validatingPath() &&
      sameAddr_(link.activePathRemote(), newRemote),
    "second candidate setup failed");
  link.pathTimeout();
  ZuCHECK(!link.validatingPath() &&
      sameAddr_(link.activePathRemote(), newRemote),
    "path-validation timeout did not retain active path");

  link.close();
}

void testAckECNValidationDisablesECN()
{
  ZuTestScope(testAckECNValidationDisablesECN);

  App app;
  ZmRef<TestLink> link = new TestLink{&app};
  link->enableECN();
  ZuCHECK(!link->ecnDisabled(), "ECN did not enable for validation test");
  ZuCHECK(link->receiveMarked(1, Zquic::EcnMark::ECT0) &&
      link->receiveMarked(2, Zquic::EcnMark::CE),
    "runtime ECN receive marking failed");
  Zquic::RuntimeDiag diag = link->runtimeDiag();
  ZuCHECK(diag.ecnRx[Zquic::CryptoLevel::OneRTT].ect0 == 1 &&
      diag.ecnRx[Zquic::CryptoLevel::OneRTT].ce == 1,
    "runtime ECN receive diagnostics mismatch");
  Zquic::PktBuild build;
  ZuCHECK(link->writePendingAck(build) &&
      build.count() == 1 &&
      build.data()[0].len > 0,
    "runtime pending ACK_ECN build failed");
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuCSpan{
	reinterpret_cast<const char *>(build.data()[0].base),
	unsigned(build.data()[0].len)},
      frame, used) &&
      frame.type == Zquic::FrameType::Ack &&
      frame.ackECN.ect0 == 1 &&
      frame.ackECN.ce == 1,
    "runtime received ECN marks were not emitted in ACK_ECN");

  link->ackECN(9, 2, 1, 0);
  diag = link->runtimeDiag();
  ZuCHECK(!link->ecnDisabled() &&
      diag.peerAckECN[Zquic::CryptoLevel::OneRTT].ect0 == 2 &&
      diag.peerAckECN[Zquic::CryptoLevel::OneRTT].ect1 == 1,
    "valid ACK_ECN did not update runtime diagnostics");
  link->ackECN(9, 1, 1, 0);
  diag = link->runtimeDiag();
  ZuCHECK(link->ecnDisabled() && diag.ecnValidationFailures == 1,
    "regressing ACK_ECN did not disable ECN");
  link->close();

  TestLink impossible{&app};
  impossible.enableECN();
  impossible.sendAckEliciting(0);
  diag = impossible.runtimeDiag();
  uint64_t inFlight = diag.congestionBytesInFlight;
  unsigned queued = impossible.txFlushQueued;
  impossible.ackECN(0, 2, 0, 0);
  diag = impossible.runtimeDiag();
  ZuCHECK(impossible.ecnDisabled() &&
      diag.ecnValidationFailures == 1 &&
      diag.congestionBytesInFlight < inFlight &&
      impossible.txFlushQueued == queued + 1,
    "impossible ACK_ECN did not disable ECN while preserving ACK processing");
  impossible.close();
}

void testBlockedFrameDuplicateSuppression()
{
  ZuTestScope(testBlockedFrameDuplicateSuppression);

  App app;
  TestLink client{&app};
  ZuCHECK(client.queueDataBlocked(1024) &&
      !client.queueDataBlocked(1024) &&
      client.queuedControlFrames() == 1,
    "pending DATA_BLOCKED duplicate was queued");
  (void)client.flushControlSends();
  ZuCHECK(
      !client.queuedControlFrames() &&
      !client.queueDataBlocked(1024) &&
      client.queueDataBlocked(2048) &&
      client.queuedControlFrames() == 1,
    "sent DATA_BLOCKED duplicate suppression mismatch");

  TestLink streamLink{&app};
  auto stream = streamLink.stream(Zi::StreamType::Duplex);
  ZuCHECK(stream &&
      streamLink.queueStreamDataBlocked(uint64_t(stream->id()), 4096) &&
      !streamLink.queueStreamDataBlocked(uint64_t(stream->id()), 4096) &&
      streamLink.queuedControlFrames() == 1,
    "pending STREAM_DATA_BLOCKED duplicate was queued");
  (void)streamLink.flushControlSends();
  ZuCHECK(
      !streamLink.queuedControlFrames() &&
      !streamLink.queueStreamDataBlocked(uint64_t(stream->id()), 4096) &&
      streamLink.queueStreamDataBlocked(uint64_t(stream->id()), 8192) &&
      streamLink.queuedControlFrames() == 1,
    "sent STREAM_DATA_BLOCKED duplicate suppression mismatch");

  TestLink limitLink{&app};
  ZuCHECK(limitLink.queueStreamsBlocked(Zi::StreamType::Duplex, 7) &&
      !limitLink.queueStreamsBlocked(Zi::StreamType::Duplex, 7) &&
      limitLink.queuedControlFrames() == 1,
    "pending STREAMS_BLOCKED duplicate was queued");
  (void)limitLink.flushControlSends();
  ZuCHECK(
      !limitLink.queuedControlFrames() &&
      limitLink.queueStreamsBlocked(Zi::StreamType::Duplex, 7) &&
      limitLink.queuedControlFrames() == 1,
    "sent STREAMS_BLOCKED was not requeued");
  (void)limitLink.flushControlSends();
  ZuCHECK(
      !limitLink.queuedControlFrames() &&
      limitLink.queueStreamsBlocked(Zi::StreamType::Duplex, 8) &&
      limitLink.queuedControlFrames() == 1,
    "sent STREAMS_BLOCKED duplicate suppression mismatch");
}

void testPeerStreamAcceptance()
{
  ZuTestScope(testPeerStreamAcceptance);

  App app;
  TestLink server{&app, true};

  Zquic::Frame frame;
  unsigned used = 0;
  auto packet = streamPkt_(0, 0, "req", true, frame, used);
  ZuCHECK(packet, "client STREAM frame setup failed");

  Zquic::BufDiag diag;
  ZuCHECK(server.receiveFrame(frame, packet, &diag) == 0,
    "server failed to receive peer STREAM frame");
  auto accepted = server.findStream(0);
  ZuCHECK(accepted && server.streamedCount == 1 &&
      server.lastStream == accepted &&
      accepted->processed == 1 &&
      accepted->rxComplete() &&
      accepted->finalSize() == 3,
    "peer stream acceptance state mismatch");

  TestLink client{&app};
  auto local = client.stream(Zi::StreamType::Duplex);
  packet = streamPkt_(local->id(), 0, "rsp", true, frame, used);
  ZuCHECK(packet,
    "response STREAM frame setup failed");
  ZuCHECK(client.receiveFrame(frame, packet, &diag) == 0 &&
      !client.streamedCount &&
      local->processed == 1 &&
      local->rxComplete(),
    "existing local bidi stream receive mismatch");

  packet = streamPkt_(1, 0, "bad", false, frame, used);
  ZuCHECK(packet,
    "local-origin STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame, packet, &diag) < 0 && !server.findStream(1),
    "server accepted local-origin peer stream ID");
}

void testStreamCountLimits()
{
  ZuTestScope(testStreamCountLimits);

  App app;
  TestLink client{&app};
  client.setPeerStreamLimit(Zi::StreamType::Duplex, 1);

  auto first = client.stream(Zi::StreamType::Duplex);
  ZuCHECK(first && first->id() == 0 &&
      client.localStreamsOpened(Zi::StreamType::Duplex) == 1 &&
      client.streamCount() == 1,
    "first stream under peer limit did not open");
  auto blocked = client.stream(Zi::StreamType::Duplex);
  ZuCHECK(!blocked &&
      client.localStreamsBlocked(Zi::StreamType::Duplex) &&
      client.queuedLocalStreams(Zi::StreamType::Duplex) == 1 &&
      client.streamCount() == 1,
    "stream over peer limit was not queued");

  uint8_t b[32];
  int n = Zquic::FrameCodec::writeMaxStreams(
    b, sizeof(b), Zi::StreamType::Duplex, 2);
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{b, unsigned(n)}, frame, used),
    "MAX_STREAMS setup failed");
  ZuCHECK(client.applyMaxStreams(frame) &&
      !client.queuedLocalStreams(Zi::StreamType::Duplex) &&
      client.localStreamsOpened(Zi::StreamType::Duplex) == 2 &&
      client.streamCount() == 2 &&
      client.streamedCount == 1 &&
      client.lastStream &&
      client.lastStream->id() == 4,
    "MAX_STREAMS did not open queued local stream");

  TestLink server{&app, true};
  server.setLocalStreamLimit(Zi::StreamType::Duplex, 1);
  auto packet = streamPkt_(0, 0, "a", true, frame, used);
  ZuCHECK(packet,
    "first peer STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame, packet) == 0 &&
      server.peerStreamsOpened(Zi::StreamType::Duplex) == 1 &&
      server.findStream(0),
    "first peer stream under local limit did not open");

  packet = streamPkt_(4, 0, "b", true, frame, used);
  ZuCHECK(packet,
    "second peer STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame, packet) < 0 &&
      server.peerStreamsOpened(Zi::StreamType::Duplex) == 1 &&
      !server.findStream(4),
    "peer stream count limit was not enforced");

  server.setLocalStreamLimit(Zi::StreamType::Duplex, 2);
  ZuCHECK(server.receiveFrame(frame, packet) == 0 &&
      server.peerStreamsOpened(Zi::StreamType::Duplex) == 2 &&
      server.findStream(4),
    "extended local stream count did not admit peer stream");

  server.setLocalStreamLimit(Zi::StreamType::Simplex, 1);
  packet = streamPkt_(2, 0, "u", true, frame, used);
  ZuCHECK(packet,
    "first peer uni STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame, packet) == 0 &&
      server.peerStreamsOpened(Zi::StreamType::Simplex) == 1 &&
      server.findStream(2),
    "first peer uni stream under local limit did not open");

  packet = streamPkt_(6, 0, "v", true, frame, used);
  ZuCHECK(packet,
    "second peer uni STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame, packet) == 0 &&
      server.peerStreamsOpened(Zi::StreamType::Simplex) == 2 &&
      server.localStreamLimit(Zi::StreamType::Simplex) == 3 &&
      server.findStream(6),
    "completed peer uni stream did not return stream-count credit");
}

void testResetStopFrames()
{
  ZuTestScope(testResetStopFrames);

  App app;
  uint8_t b[128];
  Zquic::Frame frame;
  unsigned used = 0;

  TestLink server{&app, true};
  int n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 0);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{b, unsigned(n)}, frame, used),
    "RESET_STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame) == 0, "server rejected peer RESET_STREAM");
  auto reset = server.findStream(0);
  ZuCHECK(reset &&
      server.streamedCount == 1 &&
      reset->resetReceived() &&
      reset->error() == Zquic::StreamError::Reset &&
      reset->appError() == 7 &&
      reset->rxComplete() &&
      !reset->finalSize(),
    "RESET_STREAM state mismatch");

  TestLink client{&app};
  auto local = client.stream(Zi::StreamType::Duplex);
  n = Zquic::FrameCodec::writeStopSending(b, sizeof(b), local->id(), 9);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{b, unsigned(n)}, frame, used),
    "STOP_SENDING setup failed");
  ZuCHECK(client.receiveFrame(frame) == 0 &&
      local->stopReceived() &&
      local->error() == Zquic::StreamError::Stop &&
      local->appError() == 9,
    "STOP_SENDING state mismatch");

  auto delivered = client.stream(Zi::StreamType::Duplex);
  auto packet = streamPkt_(delivered->id(), 0, "hello", false, frame, used);
  ZuCHECK(packet &&
      delivered->receiveFrame(frame, packet),
    "delivered STREAM setup failed");
  n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), delivered->id(), 1, 3);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{b, unsigned(n)}, frame, used),
    "final-size violating RESET_STREAM setup failed");
  ZuCHECK(!delivered->receiveReset(frame),
    "RESET_STREAM final-size violation was accepted");
}

void testLocalResetStopSend()
{
  ZuTestScope(testLocalResetStopSend);

  App app;
  TestLink link{&app};
  Zquic::SentFrameRef refs[2];

  auto reset = link.stream(Zi::StreamType::Duplex);
  {
    auto tx = reset->txStream_();
    tx << "abc" << Zi::flush();
  }
  reset->reset(42);
  ZuCHECK(reset->resetSent() &&
      reset->error() == Zquic::StreamError::Reset &&
      reset->appError() == 42 &&
      link.queuedControlFrames() == 1,
    "local RESET_STREAM did not queue control state");
  unsigned n = link.flushSentRefs(refs, 2);
  ZuCHECK(n == 1 &&
      refs[0].kind == Zquic::SentFrameKind::Control &&
      refs[0].controlType == Zquic::FrameType::ResetStream &&
      refs[0].streamID == uint64_t(reset->id()) &&
      refs[0].value == 42 &&
      refs[0].length == 3 &&
      !link.queuedControlFrames(),
    "local RESET_STREAM sent ref mismatch");
  Zquic::Frame rebuilt;
  ZuCHECK(link.rebuildControl(refs[0], rebuilt) &&
      rebuilt.type == Zquic::FrameType::ResetStream &&
      rebuilt.streamID == uint64_t(reset->id()) &&
      rebuilt.errorCode == 42 &&
      rebuilt.length == 3,
    "local RESET_STREAM retransmit rebuild failed");

  auto stop = link.stream(Zi::StreamType::Duplex);
  stop->stop(9);
  ZuCHECK(stop->stopSent() &&
      stop->error() == Zquic::StreamError::Stop &&
      stop->appError() == 9 &&
      link.queuedControlFrames() == 1,
    "local STOP_SENDING did not queue control state");
  n = link.flushSentRefs(refs, 2);
  ZuCHECK(n == 1 &&
      refs[0].kind == Zquic::SentFrameKind::Control &&
      refs[0].controlType == Zquic::FrameType::StopSending &&
      refs[0].streamID == uint64_t(stop->id()) &&
      refs[0].value == 9,
    "local STOP_SENDING sent ref mismatch");
  ZuCHECK(link.rebuildControl(refs[0], rebuilt) &&
      rebuilt.type == Zquic::FrameType::StopSending &&
      rebuilt.streamID == uint64_t(stop->id()) &&
      rebuilt.errorCode == 9,
    "local STOP_SENDING retransmit rebuild failed");

  Zquic::SentPktTracker acked;
  Zquic::SentPkt p0{
    1, Zquic::timeUS(1000), 100, Zquic::PktSpace::AppData,
    true, true, false};
  Zquic::SentPkt p1{
    2, Zquic::timeUS(1001), 100, Zquic::PktSpace::AppData,
    true, true, false};
  ZuCHECK(p0.addFrame(refs[0]) && p1.addFrame(refs[0]) &&
      acked.add(p0) && acked.add(p1) &&
      acked.ack(1) && acked.lose(2),
    "local STOP_SENDING ACK/loss tracker setup failed");
  Zquic::SentFrameRef ref;
  ZuCHECK(!acked.nextRetransmit(ref),
    "ACKed STOP_SENDING was retransmitted after later loss");

  Zquic::SentPktTracker lost;
  Zquic::SentPkt p2{
    3, Zquic::timeUS(1002), 100, Zquic::PktSpace::AppData,
    true, true, false};
  ZuCHECK(p2.addFrame(refs[0]) && lost.add(p2) && lost.lose(3) &&
      lost.nextRetransmit(ref) &&
      ref.kind == Zquic::SentFrameKind::Control &&
      ref.controlType == Zquic::FrameType::StopSending &&
      ref.streamID == uint64_t(stop->id()) &&
      !lost.nextRetransmit(ref),
    "lost STOP_SENDING did not queue one retransmit");
}

void testMaxAndBlockedFrameValidation()
{
  ZuTestScope(testMaxAndBlockedFrameValidation);

  App app;
  uint8_t b[128];
  Zquic::Frame frame;

  TestLink client{&app};
  auto localBidi = client.stream(Zi::StreamType::Duplex);
  auto localUni = client.stream(Zi::StreamType::Simplex);
  int n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), localBidi->id(), 4096);
  ZuCHECK(parseFrame_(b, n, frame) && client.applyMaxStreamData(frame) &&
      localBidi->txCreditLimit() == 4096,
    "MAX_STREAM_DATA did not extend local bidirectional stream credit");

  n = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 8, 4096);
  ZuCHECK(parseFrame_(b, n, frame) && !client.applyMaxStreamData(frame),
    "MAX_STREAM_DATA for uninitiated local stream was accepted");

  n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), localUni->id(), 4096);
  ZuCHECK(parseFrame_(b, n, frame) && client.applyMaxStreamData(frame),
    "MAX_STREAM_DATA for local unidirectional sender was rejected");

  n = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 3, 4096);
  ZuCHECK(parseFrame_(b, n, frame) && !client.applyMaxStreamData(frame),
    "MAX_STREAM_DATA for peer unidirectional stream was accepted");

  TestLink server{&app, true};
  server.setLocalStreamLimit(Zi::StreamType::Duplex, 2);
  n = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 0, 2048);
  ZuCHECK(parseFrame_(b, n, frame) && server.applyMaxStreamData(frame) &&
      server.findStream(0) &&
      server.peerStreamsOpened(Zi::StreamType::Duplex) == 1,
    "MAX_STREAM_DATA did not create valid peer bidirectional stream");

  server.setLocalStreamLimit(Zi::StreamType::Duplex, 1);
  n = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 4, 2048);
  ZuCHECK(parseFrame_(b, n, frame) && !server.applyMaxStreamData(frame) &&
      !server.findStream(4),
    "MAX_STREAM_DATA created peer stream beyond local stream limit");

  n = Zquic::FrameCodec::writeDataBlocked(
    b, sizeof(b), server.rxDataCreditLimit());
  ZuCHECK(parseFrame_(b, n, frame) && server.receiveDataBlocked(frame),
    "DATA_BLOCKED at local receive limit was rejected");
  n = Zquic::FrameCodec::writeDataBlocked(
    b, sizeof(b), server.rxDataCreditLimit() + 1);
  ZuCHECK(parseFrame_(b, n, frame) && !server.receiveDataBlocked(frame),
    "DATA_BLOCKED above local receive limit was accepted");

  n = Zquic::FrameCodec::writeStreamsBlocked(
    b, sizeof(b), Zi::StreamType::Duplex,
    server.localStreamLimit(Zi::StreamType::Duplex));
  ZuCHECK(parseFrame_(b, n, frame) && server.receiveStreamsBlocked(frame),
    "STREAMS_BLOCKED at local advertised peer limit was rejected");
  n = Zquic::FrameCodec::writeStreamsBlocked(
    b, sizeof(b), Zi::StreamType::Duplex,
    server.localStreamLimit(Zi::StreamType::Duplex) + 1);
  ZuCHECK(parseFrame_(b, n, frame) && !server.receiveStreamsBlocked(frame),
    "STREAMS_BLOCKED above local advertised peer limit was accepted");
}

void testStreamDataBlockedValidation()
{
  ZuTestScope(testStreamDataBlockedValidation);

  App app;
  uint8_t b[128];
  Zquic::Frame frame;

  TestLink server{&app, true};
  server.setLocalStreamLimit(Zi::StreamType::Duplex, 2);
  int n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 0, 128);
  ZuCHECK(parseFrame_(b, n, frame) &&
      server.receiveStreamDataBlocked(frame) &&
      server.findStream(0) &&
      server.findStream(0)->rxCreditUsed() == 0 &&
      server.rxDataCreditUsed() == 0,
    "valid STREAM_DATA_BLOCKED mutated receive credit");

  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 0, 64);
  ZuCHECK(parseFrame_(b, n, frame) &&
      server.receiveStreamDataBlocked(frame) &&
      server.findStream(0)->rxCreditUsed() == 0 &&
      server.rxDataCreditUsed() == 0,
    "decreasing STREAM_DATA_BLOCKED mutated receive credit");

  n = Zquic::FrameCodec::writeStreamDataBlocked(
    b, sizeof(b), 0, server.findStream(0)->rxCreditLimit() + 1);
  ZuCHECK(parseFrame_(b, n, frame) &&
      !server.receiveStreamDataBlocked(frame),
    "STREAM_DATA_BLOCKED beyond stream receive limit was accepted");

  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 4, 128);
  ZuCHECK(parseFrame_(b, n, frame) &&
      server.receiveStreamDataBlocked(frame) &&
      server.findStream(4) &&
      server.peerStreamsOpened(Zi::StreamType::Duplex) == 2,
    "valid unopened peer STREAM_DATA_BLOCKED did not create stream");

  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 8, 128);
  ZuCHECK(parseFrame_(b, n, frame) &&
      !server.receiveStreamDataBlocked(frame) &&
      !server.findStream(8),
    "STREAM_DATA_BLOCKED opened stream beyond stream-count limit");

  TestLink client{&app};
  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 2, 128);
  ZuCHECK(parseFrame_(b, n, frame) &&
      !client.receiveStreamDataBlocked(frame),
    "STREAM_DATA_BLOCKED for local unidirectional stream was accepted");

  TestLink small{&app, true};
  small.setLocalStreamLimit(Zi::StreamType::Duplex, 1);
  n = Zquic::FrameCodec::writeStreamDataBlocked(
    b, sizeof(b), 0, small.rxDataCreditLimit() + 1);
  ZuCHECK(parseFrame_(b, n, frame) &&
      !small.receiveStreamDataBlocked(frame),
    "STREAM_DATA_BLOCKED beyond connection receive limit was accepted");
}

void testInvalidClosedStreamActivity()
{
  ZuTestScope(testInvalidClosedStreamActivity);

  App app;
  uint8_t b[128];
  Zquic::Frame frame;
  unsigned used = 0;

  TestLink maxLink{&app};
  auto local = maxLink.stream(Zi::StreamType::Duplex);
  local->reset(1);
  int n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), uint64_t(local->id()), 4096);
  ZuCHECK(parseFrame_(b, n, frame) &&
      maxLink.applyMaxStreamData(frame) &&
      !maxLink.closeError(),
    "closed-stream MAX_STREAM_DATA was not ignored");
  Zquic::RuntimeDiag diag = maxLink.runtimeDiag();
  ZuCHECK(diag.invalidStreamFrames == 1 &&
      diag.closedStreamFrames == 1 &&
      !diag.suspiciousStreamCloses,
    "closed-stream MAX_STREAM_DATA diagnostics mismatch");

  TestLink resetLink{&app, true};
  n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 0);
  ZuCHECK(parseFrame_(b, n, frame) &&
      resetLink.receiveFrame(frame) == 0 &&
      resetLink.receiveFrame(frame) == 0,
    "duplicate RESET_STREAM final size was not ignored");
  diag = resetLink.runtimeDiag();
  ZuCHECK(diag.invalidStreamFrames == 1 &&
      diag.closedStreamFrames == 1 &&
      !resetLink.closeError(),
    "duplicate RESET_STREAM diagnostics mismatch");
  n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 1);
  ZuCHECK(parseFrame_(b, n, frame) &&
      resetLink.receiveFrame(frame) < 0 &&
      resetLink.closeError() == Zquic::TransportError::FinalSize,
    "duplicate RESET_STREAM final-size violation did not close");
  diag = resetLink.runtimeDiag();
  ZuCHECK(diag.suspiciousStreamCloses == 1,
    "final-size violation close diagnostics mismatch");

  TestLink blockedLink{&app, true};
  n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 0);
  ZuCHECK(parseFrame_(b, n, frame) &&
      blockedLink.receiveFrame(frame) == 0,
    "closed STREAM_DATA_BLOCKED reset setup failed");
  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 0, 0);
  ZuCHECK(parseFrame_(b, n, frame) &&
      blockedLink.receiveStreamDataBlocked(frame) &&
      !blockedLink.closeError(),
    "closed-stream STREAM_DATA_BLOCKED was not ignored");
  diag = blockedLink.runtimeDiag();
  ZuCHECK(diag.invalidStreamFrames == 1 &&
      diag.closedStreamFrames == 1,
    "closed-stream STREAM_DATA_BLOCKED diagnostics mismatch");

  TestLink dupLink{&app};
  auto dup = dupLink.stream(Zi::StreamType::Duplex);
  auto packet = streamPkt_(dup->id(), 0, "dup", false, frame, used);
  ZuCHECK(packet &&
      dupLink.receiveFrame(frame, packet) == 0,
    "duplicate STREAM setup failed");
  packet = streamPkt_(dup->id(), 0, "dup", false, frame, used);
  ZuCHECK(packet &&
      dupLink.receiveFrame(frame, packet) == 0 &&
      !dupLink.runtimeDiag().invalidStreamFrames,
    "ordinary duplicate STREAM was treated as invalid");

  TestLink threshold{&app};
  auto noisy = threshold.stream(Zi::StreamType::Duplex);
  noisy->reset(3);
  n = Zquic::FrameCodec::writeMaxStreamData(
    b, sizeof(b), uint64_t(noisy->id()), 4096);
  ZuCHECK(parseFrame_(b, n, frame), "threshold frame setup failed");
  for (unsigned i = 0; i < TestLink::suspiciousStreamThreshold(); ++i)
    ZuCHECK(threshold.applyMaxStreamData(frame),
      "closed-stream threshold frame was rejected");
  diag = threshold.runtimeDiag();
  ZuCHECK(threshold.closeError() == Zquic::TransportError::StreamState &&
      diag.suspiciousStreamCloses == 1,
    "closed-stream threshold did not close once");
  ZuCHECK(threshold.applyMaxStreamData(frame) &&
      threshold.runtimeDiag().suspiciousStreamCloses == 1,
    "closed-stream threshold produced repeated close diagnostics");
}

void testFrameRoleAndSpaceLegality()
{
  ZuTestScope(testFrameRoleAndSpaceLegality);

  App app;
  TestLink client{&app};
  TestLink server{&app, true};
  Zquic::Frame frame;

  frame.type = Zquic::FrameType::Ping;
  ZuCHECK(client.frameLegal(Zquic::CryptoLevel::Initial, frame) &&
      client.frameLegal(Zquic::CryptoLevel::Handshake, frame) &&
      client.frameLegal(Zquic::CryptoLevel::OneRTT, frame),
    "PING frame-space legality mismatch");

  frame.reset();
  frame.type = Zquic::FrameType::Stream;
  ZuCHECK(!client.frameLegal(Zquic::CryptoLevel::Initial, frame) &&
      !client.frameLegal(Zquic::CryptoLevel::Handshake, frame) &&
      client.frameLegal(Zquic::CryptoLevel::OneRTT, frame),
    "STREAM frame-space legality mismatch");

  frame.reset();
  frame.type = Zquic::FrameType::Crypto;
  ZuCHECK(client.frameLegal(Zquic::CryptoLevel::Initial, frame) &&
      client.frameLegal(Zquic::CryptoLevel::Handshake, frame) &&
      !client.frameLegal(Zquic::CryptoLevel::OneRTT, frame),
    "CRYPTO frame-space legality mismatch");

  frame.reset();
  frame.type = Zquic::FrameType::Ack;
  frame.ackRanges.push(Zquic::AckRange{0, 0});
  ZuCHECK(!client.frameLegal(Zquic::CryptoLevel::Initial, frame),
    "ACK for unsent packet number was accepted");

  frame.reset();
  frame.type = Zquic::FrameType::NewToken;
  frame.payload = ZuCSpan{"token", 5};
  ZuCHECK(client.frameLegal(Zquic::CryptoLevel::OneRTT, frame) &&
      !server.frameLegal(Zquic::CryptoLevel::OneRTT, frame) &&
      !client.frameLegal(Zquic::CryptoLevel::Handshake, frame),
    "NEW_TOKEN role/space legality mismatch");
  frame.payload = {};
  ZuCHECK(!client.frameLegal(Zquic::CryptoLevel::OneRTT, frame),
    "empty NEW_TOKEN was accepted");

  frame.reset();
  frame.type = Zquic::FrameType::HandshakeDone;
  ZuCHECK(client.frameLegal(Zquic::CryptoLevel::OneRTT, frame) &&
      !server.frameLegal(Zquic::CryptoLevel::OneRTT, frame) &&
      !client.frameLegal(Zquic::CryptoLevel::Handshake, frame),
    "HANDSHAKE_DONE role/space legality mismatch");

  frame.reset();
  frame.type = Zquic::FrameType::ApplicationClose;
  ZuCHECK(!client.frameLegal(Zquic::CryptoLevel::Initial, frame) &&
      client.frameLegal(Zquic::CryptoLevel::OneRTT, frame),
    "APPLICATION_CLOSE frame-space legality mismatch");

  frame.reset();
  frame.type = Zquic::FrameType::Unknown;
  ZuCHECK(!client.frameLegal(Zquic::CryptoLevel::OneRTT, frame),
    "unknown extension frame was accepted");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testStreamIDs);
  ZuTestCall(testStreamFrameDelivery);
  ZuTestCall(testStreamRxSliceDelivery);
  ZuTestCall(testOutOfOrderStreamDelivery);
  ZuTestCall(testStreamTxRetention);
  ZuTestCall(testStreamPktizer);
  ZuTestCall(testQueuedControlSendFailureRetainsFrame);
  ZuTestCall(testRuntimeMultiFrameAssembly);
  ZuTestCall(testRuntimePacketNumberLength);
  ZuTestCall(testLongHeaderCoalescing);
  ZuTestCall(testCongestionBudgetGatesRuntimeSends);
  ZuTestCall(testActivePathRuntimeBudget);
  ZuTestCall(testPathValidationStateMachine);
  ZuTestCall(testAckECNValidationDisablesECN);
  ZuTestCall(testBlockedFrameDuplicateSuppression);
  ZuTestCall(testPeerStreamAcceptance);
  ZuTestCall(testStreamCountLimits);
  ZuTestCall(testResetStopFrames);
  ZuTestCall(testLocalResetStopSend);
  ZuTestCall(testMaxAndBlockedFrameValidation);
  ZuTestCall(testStreamDataBlockedValidation);
  ZuTestCall(testInvalidClosedStreamActivity);
  ZuTestCall(testFrameRoleAndSpaceLegality);
}
