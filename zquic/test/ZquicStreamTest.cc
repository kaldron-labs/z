//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>
#include <zlib/ZquicSched.hh>

using namespace ZuTestUtil;

using StreamTxBufAlloc = Zquic::StreamTxBufAlloc<>;

struct App { };
struct TestLink;
struct TestStream :
  public Zquic::Stream<TestLink, TestStream, StreamTxBufAlloc>
{
  using Base = Zquic::Stream<TestLink, TestStream, StreamTxBufAlloc>;
  using Base::Base;
  int process(Zquic::RxStream &) { ++processed; return 0; }

  unsigned processed = 0;
};
using TestLinkRef = ZmRef<TestLink>;
using TestCxn = Zquic::Cxn<TestLink, TestLinkRef>;
using TestCxnRef = ZmRef<TestCxn>;
struct TestLink :
  public Zquic::Link<App, TestLink,
    StreamTxBufAlloc, TestCxn, TestCxnRef, TestStream>
{
  using Base = Zquic::Link<App, TestLink,
    StreamTxBufAlloc, TestCxn, TestCxnRef, TestStream>;
  TestLink(App *app, bool isServer = false) : Base{app, isServer} { }
  void streamed(ZmRef<TestStream> stream) {
    lastStream = ZuMv(stream);
    ++streamedCount;
  }

  ZmRef<TestStream>	lastStream;
  unsigned		streamedCount = 0;
};

static ZmRef<ZiIOBuf> streamPacket_(
  uint64_t id, uint64_t offset, ZuCSpan payload, bool fin,
  Zquic::Frame &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = new Zquic::PacketRxBufAlloc<>{nullptr};
  int n = Zquic::FrameCodec::writeStream(
    packet->data_(), packet->size, id, offset, payload, fin);
  if (n <= 0) return nullptr;
  packet->skip = 0;
  packet->length = unsigned(n);
  if (Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(packet->data_()),
	packet->length}, frame, used) ||
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

void testStreamIDs()
{
  ZuTestScope(testStreamIDs);

  App app;
  TestLink client{&app};
  auto c0 = client.stream(Zquic::StreamType::Bidi);
  auto c1 = client.stream(Zquic::StreamType::Uni);
  ZuCHECK(c0->id() == 0 && c1->id() == 2, "client stream IDs mismatch");

  TestLink server{&app, true};
  auto s0 = server.stream(Zquic::StreamType::Bidi);
  auto s1 = server.stream(Zquic::StreamType::Uni);
  ZuCHECK(s0->id() == 1 && s1->id() == 3, "server stream IDs mismatch");
  ZuCHECK(server.findStream(1) == s0, "stream table lookup failed");
}

void testStreamFrameDelivery()
{
  ZuTestScope(testStreamFrameDelivery);

  App app;
  TestLink client{&app};
  auto stream = client.stream(Zquic::StreamType::Bidi);

  Zquic::Frame frame;
  unsigned used = 0;
  auto packet = streamPacket_(stream->id(), 0, "hello", false, frame, used);
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

  packet = streamPacket_(stream->id(), 5, {}, true, frame, used);
  ZuCHECK(packet, "STREAM FIN frame setup failed");
  ZuCHECK(stream->processFrame(frame, packet, &diag) == 0 &&
      stream->finReceived() && stream->rxComplete() &&
      stream->finalSize() == 5,
    "STREAM FIN delivery mismatch");

  packet = streamPacket_(stream->id(), 5, "!", true, frame, used);
  ZuCHECK(packet,
    "conflicting STREAM FIN setup failed");
  ZuCHECK(!stream->receiveFrame(frame, packet, &diag),
    "conflicting final-size STREAM was accepted");

  packet = streamPacket_(stream->id() + 4, 0, "x", false, frame, used);
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
  auto stream = client.stream(Zquic::StreamType::Bidi);

  ZmRef<ZiIOBuf> packet = new Zquic::PacketRxBufAlloc<>{nullptr};
  int n = Zquic::FrameCodec::writeStream(
    packet->data_(), packet->size, stream->id(), 0, "slice", false);
  ZuCHECK(n > 0, "packet-backed STREAM frame write failed");
  packet->skip = 0;
  packet->length = unsigned(n);

  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(packet->data_()),
	packet->length}, frame, used) &&
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
  auto stream = client.stream(Zquic::StreamType::Bidi);

  Zquic::Frame frame;
  unsigned used = 0;
  Zquic::BufDiag diag;

  auto packet = streamPacket_(stream->id(), 5, "world", false, frame, used);
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

  packet = streamPacket_(stream->id(), 0, "hello", false, frame, used);
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

  auto split = client.stream(Zquic::StreamType::Bidi);
  diag = {};
  packet = streamPacket_(split->id(), 5, "world", false, frame, used);
  ZuCHECK(packet,
    "split pending STREAM setup failed");
  ZuCHECK(split->processFrame(frame, packet, &diag) == 0 &&
      split->rxPending() == 1 &&
      !split->rxQueued(),
    "split pending STREAM state mismatch");

  packet = streamPacket_(split->id(), 0, "helloworldtails", false, frame, used);
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
  auto stream = client.stream(Zquic::StreamType::Bidi);

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

void testStreamPacketizer()
{
  ZuTestScope(testStreamPacketizer);

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
      ZuCSpan{reinterpret_cast<const char *>(assembled),
	unsigned(prefixLen) + prefixPayload.length()},
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
  auto stream = client.stream(Zquic::StreamType::Bidi);
  {
    auto tx = stream->txStream_();
    tx << "abc" << Zi::flush();
    tx << "def" << Zi::flush();
  }
  stream->fin();

  uint8_t b[128];
  Zquic::PacketBudget budget;
  Zquic::PacketAssembly assembly;
  Zquic::StreamFrameInfo info;
  int n = Zquic::StreamPacketizer::writeNext(
    b, sizeof(b), budget, assembly, *stream, &info);
  ZuCHECK(n > 0 &&
      assembly.streamAdded() &&
      budget.used == unsigned(n) &&
      info.streamID == 0 &&
      info.offset == 0 &&
      info.length == 3 &&
      info.bytes == unsigned(n) &&
      !info.fin &&
      stream->txRangeCount() == 1,
    "first packetized STREAM accounting mismatch");

  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used) &&
      used == unsigned(n) &&
      frame.type == Zquic::FrameType::Stream &&
      frame.streamID == 0 &&
      frame.offset == 0 &&
      frame.length == 3 &&
      frame.payload == "abc" &&
      !frame.fin,
    "first packetized STREAM parse mismatch");
  ZuCHECK(Zquic::StreamPacketizer::writeNext(
      b, sizeof(b), budget, assembly, *stream, &info) < 0 &&
      stream->txRangeCount() == 1,
    "packetizer allowed a second STREAM frame in one packet");

  budget = {};
  assembly = {};
  n = Zquic::StreamPacketizer::writeNext(
    b, sizeof(b), budget, assembly, *stream, &info);
  ZuCHECK(n > 0 &&
      info.offset == 3 &&
      info.length == 3 &&
      info.fin &&
      !stream->txRangeCount() &&
      stream->finDequeued() &&
      !stream->finReady(),
    "second packetized STREAM accounting mismatch");
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used) &&
      frame.offset == 3 &&
      frame.length == 3 &&
      frame.payload == "def" &&
      frame.fin,
    "second packetized STREAM parse mismatch");

  budget = {};
  assembly = {};
  ZuCHECK(!Zquic::StreamPacketizer::writeNext(
      b, sizeof(b), budget, assembly, *stream, &info),
    "packetizer wrote a frame for an empty stream");

  auto blocked = client.stream(Zquic::StreamType::Bidi);
  {
    auto tx = blocked->txStream_();
    tx << "blocked" << Zi::flush();
  }
  budget = {};
  budget.pmtu = budget.congestion = budget.antiAmplification = 2;
  assembly = {};
  ZuCHECK(Zquic::StreamPacketizer::writeNext(
      b, sizeof(b), budget, assembly, *blocked, &info) < 0 &&
      blocked->txRangeCount() == 1 &&
      blocked->txBufferedBytes() == 7 &&
      !assembly.streamAdded(),
    "packetizer consumed data without packet budget");

  auto split = client.stream(Zquic::StreamType::Bidi);
  {
    auto tx = split->txStream_();
    tx << "abcdefghij" << Zi::flush();
  }
  budget = {};
  budget.pmtu = budget.congestion = budget.antiAmplification = 8;
  assembly = {};
  n = Zquic::StreamPacketizer::writeNext(
    b, sizeof(b), budget, assembly, *split, &info);
  ZuCHECK(n == 8 &&
      info.streamID == uint64_t(split->id()) &&
      info.offset == 0 &&
      info.length == 5 &&
      info.bytes == 8 &&
      split->txRangeCount() == 1 &&
      split->txBufferedBytes() == 5,
    "packetizer did not split oversized stream range");
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used) &&
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

void testPeerStreamAcceptance()
{
  ZuTestScope(testPeerStreamAcceptance);

  App app;
  TestLink server{&app, true};

  Zquic::Frame frame;
  unsigned used = 0;
  auto packet = streamPacket_(0, 0, "req", true, frame, used);
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
  auto local = client.stream(Zquic::StreamType::Bidi);
  packet = streamPacket_(local->id(), 0, "rsp", true, frame, used);
  ZuCHECK(packet,
    "response STREAM frame setup failed");
  ZuCHECK(client.receiveFrame(frame, packet, &diag) == 0 &&
      !client.streamedCount &&
      local->processed == 1 &&
      local->rxComplete(),
    "existing local bidi stream receive mismatch");

  packet = streamPacket_(1, 0, "bad", false, frame, used);
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
  client.setPeerStreamLimit(Zquic::StreamType::Bidi, 1);

  auto first = client.stream(Zquic::StreamType::Bidi);
  ZuCHECK(first && first->id() == 0 &&
      client.localStreamsOpened(Zquic::StreamType::Bidi) == 1 &&
      client.streamCount() == 1,
    "first stream under peer limit did not open");
  auto blocked = client.stream(Zquic::StreamType::Bidi);
  ZuCHECK(!blocked &&
      client.localStreamsBlocked(Zquic::StreamType::Bidi) &&
      client.queuedLocalStreams(Zquic::StreamType::Bidi) == 1 &&
      client.streamCount() == 1,
    "stream over peer limit was not queued");

  uint8_t b[32];
  int n = Zquic::FrameCodec::writeMaxStreams(
    b, sizeof(b), Zquic::StreamType::Bidi, 2);
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "MAX_STREAMS setup failed");
  ZuCHECK(client.applyMaxStreams(frame) &&
      !client.queuedLocalStreams(Zquic::StreamType::Bidi) &&
      client.localStreamsOpened(Zquic::StreamType::Bidi) == 2 &&
      client.streamCount() == 2 &&
      client.streamedCount == 1 &&
      client.lastStream &&
      client.lastStream->id() == 4,
    "MAX_STREAMS did not open queued local stream");

  TestLink server{&app, true};
  server.setLocalStreamLimit(Zquic::StreamType::Bidi, 1);
  auto packet = streamPacket_(0, 0, "a", true, frame, used);
  ZuCHECK(packet,
    "first peer STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame, packet) == 0 &&
      server.peerStreamsOpened(Zquic::StreamType::Bidi) == 1 &&
      server.findStream(0),
    "first peer stream under local limit did not open");

  packet = streamPacket_(4, 0, "b", true, frame, used);
  ZuCHECK(packet,
    "second peer STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame, packet) < 0 &&
      server.peerStreamsOpened(Zquic::StreamType::Bidi) == 1 &&
      !server.findStream(4),
    "peer stream count limit was not enforced");

  server.setLocalStreamLimit(Zquic::StreamType::Bidi, 2);
  ZuCHECK(server.receiveFrame(frame, packet) == 0 &&
      server.peerStreamsOpened(Zquic::StreamType::Bidi) == 2 &&
      server.findStream(4),
    "extended local stream count did not admit peer stream");

  server.setLocalStreamLimit(Zquic::StreamType::Uni, 1);
  packet = streamPacket_(2, 0, "u", true, frame, used);
  ZuCHECK(packet,
    "first peer uni STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame, packet) == 0 &&
      server.peerStreamsOpened(Zquic::StreamType::Uni) == 1 &&
      server.findStream(2),
    "first peer uni stream under local limit did not open");

  packet = streamPacket_(6, 0, "v", true, frame, used);
  ZuCHECK(packet,
    "second peer uni STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame, packet) < 0 &&
      server.peerStreamsOpened(Zquic::StreamType::Uni) == 1 &&
      !server.findStream(6),
    "peer uni stream count limit was not enforced");
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
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
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
  auto local = client.stream(Zquic::StreamType::Bidi);
  n = Zquic::FrameCodec::writeStopSending(b, sizeof(b), local->id(), 9);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "STOP_SENDING setup failed");
  ZuCHECK(client.receiveFrame(frame) == 0 &&
      local->stopReceived() &&
      local->error() == Zquic::StreamError::Stop &&
      local->appError() == 9,
    "STOP_SENDING state mismatch");

  auto delivered = client.stream(Zquic::StreamType::Bidi);
  auto packet = streamPacket_(delivered->id(), 0, "hello", false, frame, used);
  ZuCHECK(packet &&
      delivered->receiveFrame(frame, packet),
    "delivered STREAM setup failed");
  n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), delivered->id(), 1, 3);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "final-size violating RESET_STREAM setup failed");
  ZuCHECK(!delivered->receiveReset(frame),
    "RESET_STREAM final-size violation was accepted");
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
  ZuTestCall(testStreamPacketizer);
  ZuTestCall(testPeerStreamAcceptance);
  ZuTestCall(testStreamCountLimits);
  ZuTestCall(testResetStopFrames);
}
