//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

class FlowApp : public Zquic::Hub<FlowApp> {
public:
  using Base = Zquic::Hub<FlowApp>;

  FlowApp() : m_mx{mxParams()} {
    ZiAssert(Base::init(Zquic::HubParams(&m_mx, "3", "4")),
      "Zquic", (), "flow test app init failed", return);
    ZiAssert(m_mx.start(), "Zquic", (),
      "flow test multiplexer start failed", return);
  }
  ~FlowApp() {
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
  static ZiMxParams mxParams() {
    return ZiMxParams()
      .scheduler([](auto &s) {
	s.nThreads(4);
      })
      .rxThread(1).txThread(2);
  }

  ZiMultiplex	m_mx;
};
struct FlowLink;
struct FlowStream :
  public Zquic::Stream<FlowLink, FlowStream>
{
  using Base = Zquic::Stream<FlowLink, FlowStream>;
  FlowStream(int64_t id) : Base{nullptr, id} { }
  using Base::Base;
  int process(Zquic::RxStream &) { return 0; }
};
struct FlowLink :
  public Zquic::Link<FlowApp, FlowLink, FlowStream>
{
  using Base = Zquic::Link<FlowApp, FlowLink, FlowStream>;
  using Base::Base;
  void flushTx_(ZiSockAddr = {}) { } // This flow-only fixture has no transport output.
};

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

void testFlowCreditAndLimits()
{
  ZuTestScope(testFlowCreditAndLimits);

  Zquic::FlowCredit credit{100};
  ZuCHECK(credit.consume(60), "flow consume failed");
  ZuCHECK(credit.allows(40) && !credit.allows(41),
    "flow availability check mismatch");
  ZuCHECK(credit.shouldUpdate(), "flow update threshold not reached");
  ZuCHECK(!credit.consume(41), "flow overrun accepted");
  credit.extend(200);
  ZuCHECK(credit.consume(140) && credit.blocked(),
    "flow extension/blocking mismatch");
  credit.extend(300);
  ZuCHECK(credit.consumeTo(250) && credit.used() == 250,
    "flow consume-to failed");

  Zquic::StreamLimit limit{2};
  ZuCHECK(limit.open() && limit.open() && limit.blocked(),
    "stream limit open mismatch");
  ZuCHECK(!limit.open(), "blocked stream limit opened");
  limit.extend(3);
  ZuCHECK(limit.open(), "extended stream limit did not open");
}

void testFlowControlFrames()
{
  ZuTestScope(testFlowControlFrames);

  uint8_t b[64];
  Zquic::Frame f;
  unsigned used = 0;

  int n = Zquic::FrameCodec::writeDataBlocked(b, sizeof(b), 100);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
    ZuBSpan{b, unsigned(n)}, f, used) &&
    f.type == Zquic::FrameType::DataBlocked && f.value == 100,
    "DATA_BLOCKED frame mismatch");

  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 4, 200);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
    ZuBSpan{b, unsigned(n)}, f, used) &&
    f.type == Zquic::FrameType::StreamDataBlocked &&
    f.streamID == 4 && f.value == 200,
	    "STREAM_DATA_BLOCKED frame mismatch");
}

void testStreamBlockedAdvisory()
{
  ZuTestScope(testStreamBlockedAdvisory);

  FlowApp app;
  FlowLink link{&app};
  FlowStream stream{&link, 4};
  stream.rxCredit(200);

  Zquic::Frame frame;
  frame.type = Zquic::FrameType::StreamDataBlocked;
  frame.streamID = 4;
  frame.value = 100;

  ZuCHECK(stream.receiveBlocked(frame) &&
      !stream.rxCreditUsed() &&
      stream.rxCreditLimit() == 200,
    "STREAM_DATA_BLOCKED mutated receive credit");

  frame.value = 201;
  ZuCHECK(!stream.receiveBlocked(frame) &&
      !stream.rxCreditUsed() &&
      stream.rxCreditLimit() == 200,
    "oversized STREAM_DATA_BLOCKED changed receive credit");
}

void testReceiveFlowControl()
{
  ZuTestScope(testReceiveFlowControl);

  Zquic::Frame frame;
  unsigned used = 0;
  Zquic::BufDiag diag;

  FlowApp app;
  FlowLink link{&app};
  ZmRef<FlowStream> stream = new FlowLink::Streams::Node{&link, 0};
  Zquic::ReceiveFlow flow{8, 5};
  auto packet = streamPkt(stream->id(), 3, "de", false, frame, used);
  ZuCHECK(packet,
    "out-of-order flow STREAM setup failed");
  ZuCHECK(stream->receiveFrame(frame, flow, packet, &diag) &&
      flow.dataUsed() == 2 &&
      flow.streamUsed() == 5 &&
      stream->rxPending() == 1,
    "out-of-order receive flow accounting mismatch");

  packet = streamPkt(stream->id(), 0, "abc", false, frame, used);
  ZuCHECK(packet,
    "gap-filling flow STREAM setup failed");
  ZuCHECK(stream->receiveFrame(frame, flow, packet, &diag) &&
      flow.dataUsed() == 5 &&
      flow.streamUsed() == 5 &&
      stream->rxBytes() == 5,
    "gap-filling receive flow accounting mismatch");

  ZuCHECK(stream->receiveFrame(frame, flow, packet, &diag) &&
      flow.dataUsed() == 5,
    "duplicate STREAM changed receive flow accounting");

  packet = streamPkt(stream->id(), 5, "x", false, frame, used);
  ZuCHECK(packet,
    "stream-limit flow violation setup failed");
  ZuCHECK(!stream->receiveFrame(frame, flow, packet, &diag) &&
      flow.error() == Zquic::TransportError::FlowControl &&
      flow.dataUsed() == 5 &&
      flow.streamUsed() == 5,
    "stream-level flow violation was not rejected cleanly");

  ZmRef<FlowStream> connLimited = new FlowLink::Streams::Node{&link, 4};
  Zquic::ReceiveFlow connFlow{6, 20};
  packet = streamPkt(connLimited->id(), 0, "hello", false, frame, used);
  ZuCHECK(packet &&
      connLimited->receiveFrame(frame, connFlow, packet, &diag),
    "connection flow first receive failed");
  packet = streamPkt(connLimited->id(), 5, "!!", false, frame, used);
  ZuCHECK(packet,
    "connection-limit flow violation setup failed");
  ZuCHECK(!connLimited->receiveFrame(frame, connFlow, packet, &diag) &&
      connFlow.error() == Zquic::TransportError::FlowControl &&
      connFlow.dataUsed() == 5 &&
      connFlow.streamUsed() == 5,
    "connection-level flow violation mutated flow state");
}

void testReceiveFlowUpdates()
{
  ZuTestScope(testReceiveFlowUpdates);

  Zquic::ReceiveFlow flow{10, 12};
  ZuCHECK(flow.receive(6, 6), "receive flow update setup failed");
  Zquic::FlowUpdate update;
  ZuCHECK(flow.maxDataUpdate(update) &&
      update.type == Zquic::FrameType::MaxData &&
      update.maximum == 16 &&
      flow.dataLimit() == 16,
    "MAX_DATA update decision mismatch");
  uint8_t b[32];
  int n = update.write(b, sizeof(b));
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used) &&
      frame.type == Zquic::FrameType::MaxData &&
      frame.value == 16,
    "MAX_DATA update frame mismatch");

  ZuCHECK(flow.maxStreamDataUpdate(4, update) &&
      update.type == Zquic::FrameType::MaxStreamData &&
      update.streamID == 4 &&
      update.maximum == 18 &&
      flow.streamLimit() == 18,
    "MAX_STREAM_DATA update decision mismatch");
  n = update.write(b, sizeof(b));
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used) &&
      frame.type == Zquic::FrameType::MaxStreamData &&
      frame.streamID == 4 &&
      frame.value == 18,
    "MAX_STREAM_DATA update frame mismatch");
}

void testScheduling()
{
  ZuTestScope(testScheduling);

  Zquic::StreamScheduler streams;
  ZuCHECK(streams.add(4) && streams.add(8) && streams.add(4) &&
    streams.count() == 2, "stream scheduler duplicate handling failed");
  ZuCHECK(streams.next() == 4 && streams.next() == 8,
    "stream scheduler round robin mismatch");
  bool scalableAdd = true;
  for (unsigned i = 0; i < 128; ++i)
    scalableAdd &= streams.add(1000 + i);
  ZuCHECK(scalableAdd && streams.count() == 130 && streams.contains(1127),
    "stream scheduler hash membership mismatch");
  streams.remove(4);
  streams.remove(1064);
  ZuCHECK(!streams.contains(4) && !streams.contains(1064) &&
      streams.count() == 128,
    "stream scheduler hash removal mismatch");

  Zquic::TxScheduler tx;
  ZuCHECK(tx.addStream(4) && tx.addControl(2) && tx.next() == 2,
    "control stream priority mismatch");
  tx.remove(2);
  ZuCHECK(tx.next() == 4, "data stream scheduling mismatch");

  Zquic::PktBudget budget;
  budget.pmtu = 50;
  budget.congestion = 50;
  budget.antiAmplification = 50;
  ZuCHECK(budget.add(10) &&
    budget.add(20) &&
    budget.add(1) &&
    budget.add(19) &&
    !budget.remaining(), "packet assembly budget/STREAM rules mismatch");

  Zquic::ControlFrame control = Zquic::ControlFrame::blocked(
    Zquic::FrameType::StreamDataBlocked, 4, 1024);
  uint8_t b[32];
  int n = control.write(b, sizeof(b));
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used) &&
      frame.type == Zquic::FrameType::StreamDataBlocked &&
      frame.streamID == 4 &&
      frame.value == 1024,
    "queued STREAM_DATA_BLOCKED control write mismatch");

  control = Zquic::ControlFrame::flowUpdate(
    Zquic::FlowUpdate{
      Zquic::FrameType::MaxStreams, 0, 8, Zquic::StreamType::Simplex});
  n = control.write(b, sizeof(b));
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuBSpan{b, unsigned(n)}, frame, used) &&
      frame.type == Zquic::FrameType::MaxStreams &&
      frame.streamType == Zquic::StreamType::Simplex &&
      frame.value == 8,
    "queued MAX_STREAMS control write mismatch");
}

void testStreamPrefixFit()
{
  ZuTestScope(testStreamPrefixFit);

  FlowApp app;
  auto backing = app.streamTxAllocator()->allocBuf(nullptr, 16384);
  memset(backing->data(), 0x5a, backing->length);
  struct Stream {
    Zquic::TxRange range;
    uint64_t credit;
    int64_t streamID;
    bool ended = false;
    unsigned committed = 0;
    int64_t id() const { return streamID; }
    uint64_t txCreditAvailable() const { return credit; }
    bool consumeTxCredit(unsigned n) { credit -= n; return true; }
    bool finSent() const { return true; }
    unsigned txRangeCount() const { return 1; }
    bool nextTxRange(Zquic::PktBudget &, Zquic::TxRange &out, bool &fin) const {
      out = range; fin = false; return true;
    }
    bool commitTxRange(Zquic::TxRange &out, const Zquic::TxRange &selected, unsigned n) {
      out = selected; out.length = n; committed = n; return true;
    }
    bool dequeueFin(uint64_t &offset) {
      ended = true; offset = range.streamOffset + range.length; return true;
    }
    uint64_t txBytes() const { return range.streamOffset + range.length; }
  };
  unsigned lengths[] = {63, 64, 16383, 16384};
  for (uint64_t id : {uint64_t(4), uint64_t(1)<<30}) {
    for (uint64_t offset : {uint64_t(0), uint64_t(1)<<20}) {
      for (unsigned length : lengths) {
	unsigned fixed = 1 + Zquic::VarInt::length(id) +
	  (offset ? Zquic::VarInt::length(offset) : 0);
	unsigned payload = length > Zquic::BufSize ? Zquic::BufSize - fixed - 2 : length;
	unsigned prefix = fixed + Zquic::VarInt::length(payload);
	Stream stream{{backing, 0, length, offset}, length, int64_t(id)};
	Zquic::PktBudget budget;
	budget.pmtu = budget.congestion = budget.antiAmplification = prefix + payload;
	budget.flow = length;
	uint8_t scratch[32];
	memset(scratch, 0xa5, sizeof(scratch));
	Zquic::StreamFrameInfo info;
	int n = Zquic::StreamPktizer::writeNext(scratch, prefix, budget, stream, &info);
	ZuCHECK(n == int(prefix) && stream.committed == payload && stream.ended == (payload == length) &&
	    info.length == payload && info.fin == (payload == length) && !budget.remaining() &&
	    budget.flowRemaining() == length - payload && scratch[prefix] == 0xa5,
	  "STREAM varint boundary/minimal prefix scratch fit mismatch");
	stream = Stream{{backing, 0, length, offset}, 0, int64_t(id)};
	budget.used = 0;
	budget.flow = length;
	ZuCHECK(Zquic::StreamPktizer::writeNext(scratch, prefix, budget, stream) < 0 &&
	    !stream.committed && !stream.ended && !budget.used,
	  "zero stream credit committed output");
      }
    }
  }
}

void testPacing()
{
  ZuTestScope(testPacing);

  Zquic::Pacer pacer;
  ZuCHECK(pacer.canSend(1000), "unarmed pacer blocked send");
  pacer.sent(1000, 1200, 12000, 1000);
  ZuCHECK(pacer.interval() == 100 &&
      pacer.nextSendTime() == 1100 &&
      !pacer.canSend(1099) &&
      pacer.delay(1099) == 1 &&
      pacer.canSend(1100),
    "pacer interval/deadline mismatch");
  pacer.reset();
  ZuCHECK(!pacer.armed() && pacer.canSend(1), "pacer reset mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testFlowCreditAndLimits);
  ZuTestCall(testFlowControlFrames);
  ZuTestCall(testStreamBlockedAdvisory);
  ZuTestCall(testReceiveFlowControl);
  ZuTestCall(testReceiveFlowUpdates);
  ZuTestCall(testScheduling);
  ZuTestCall(testStreamPrefixFit);
  ZuTestCall(testPacing);
}
