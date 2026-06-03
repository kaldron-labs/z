//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>
#include <zlib/ZquicSched.hh>

using namespace ZuTestUtil;

struct App { };
struct TestStream :
  public Zquic::Stream<TestStream, Zquic::StreamBufAlloc<>,
    Zquic::StreamBufAlloc<>>
{
  using Base = Zquic::Stream<TestStream, Zquic::StreamBufAlloc<>,
    Zquic::StreamBufAlloc<>>;
  TestStream(int64_t id) : Base{id} { }
  int process(Zquic::RxStream &) { ++processed; return 0; }

  unsigned processed = 0;
};
struct TestLink;
using TestLinkRef = ZmRef<TestLink>;
using TestCxn = Zquic::Cxn<TestLink, TestLinkRef>;
using TestCxnRef = ZmRef<TestCxn>;
struct TestLink :
  public Zquic::Link<App, TestLink, Zquic::StreamBufAlloc<>,
    Zquic::StreamBufAlloc<>, TestCxn, TestCxnRef, TestStream>
{
  using Base = Zquic::Link<App, TestLink, Zquic::StreamBufAlloc<>,
    Zquic::StreamBufAlloc<>, TestCxn, TestCxnRef, TestStream>;
  TestLink(App *app, bool isServer = false) : Base{app, isServer} { }
  void streamed(ZmRef<TestStream> stream) {
    lastStream = ZuMv(stream);
    ++streamedCount;
  }

  ZmRef<TestStream>	lastStream;
  unsigned		streamedCount = 0;
};

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

  uint8_t b[128];
  int n = Zquic::FrameCodec::writeStream(b, sizeof(b), stream->id(), 0,
    "hello", false);
  ZuCHECK(n > 0, "STREAM frame write failed");

  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used) &&
      used == unsigned(n),
    "STREAM frame parse failed");

  Zquic::BufDiag diag;
  ZuCHECK(stream->processFrame(frame, &diag) == 0 &&
      stream->processed == 1 &&
      stream->rxBytes() == 5 &&
      stream->rxCopyCount() == 1 &&
      uint64_t(diag.rxPacketToStreamCopies) == 1,
    "STREAM frame delivery accounting mismatch");

  auto &rx = stream->rxStream();
  auto span = rx.span();
  ZuCHECK(span.length() == 5 && !memcmp(span.data(), "hello", 5),
    "STREAM payload was not queued for receive");
  ZuCHECK(rx.advance(5) && rx.empty(), "STREAM receive queue advance failed");

  n = Zquic::FrameCodec::writeStream(b, sizeof(b), stream->id(), 5,
    {}, true);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "STREAM FIN frame setup failed");
  ZuCHECK(stream->processFrame(frame, &diag) == 0 &&
      stream->finReceived() && stream->rxComplete() &&
      stream->finalSize() == 5 &&
      uint64_t(diag.rxPacketToStreamCopies) == 1,
    "STREAM FIN delivery mismatch");

  n = Zquic::FrameCodec::writeStream(b, sizeof(b), stream->id(), 5,
    "!", true);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "conflicting STREAM FIN setup failed");
  ZuCHECK(!stream->receiveFrame(frame, &diag),
    "conflicting final-size STREAM was accepted");

  n = Zquic::FrameCodec::writeStream(b, sizeof(b), stream->id() + 4, 0,
    "x", false);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "wrong-ID STREAM setup failed");
  ZuCHECK(!stream->receiveFrame(frame, &diag),
    "wrong-ID STREAM was accepted");
}

void testOutOfOrderStreamDelivery()
{
  ZuTestScope(testOutOfOrderStreamDelivery);

  App app;
  TestLink client{&app};
  auto stream = client.stream(Zquic::StreamType::Bidi);

  uint8_t b[128];
  Zquic::Frame frame;
  unsigned used = 0;
  Zquic::BufDiag diag;

  int n = Zquic::FrameCodec::writeStream(
    b, sizeof(b), stream->id(), 5, "world", false);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "out-of-order STREAM setup failed");
  ZuCHECK(stream->processFrame(frame, &diag) == 0 &&
      stream->processed == 1 &&
      !stream->rxBytes() &&
      stream->rxPending() == 1 &&
      !stream->rxQueued() &&
      stream->rxCopyCount() == 1 &&
      uint64_t(diag.rxPacketToStreamCopies) == 1,
    "out-of-order STREAM pending state mismatch");
  ZuCHECK(stream->processFrame(frame, &diag) == 0 &&
      stream->rxPending() == 1 &&
      stream->rxCopyCount() == 1 &&
      uint64_t(diag.rxPacketToStreamCopies) == 1,
    "duplicate pending STREAM copied or queued again");

  n = Zquic::FrameCodec::writeStream(
    b, sizeof(b), stream->id(), 0, "hello", false);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "gap-filling STREAM setup failed");
  ZuCHECK(stream->processFrame(frame, &diag) == 0 &&
      stream->rxBytes() == 10 &&
      !stream->rxPending() &&
      stream->rxQueued() == 2 &&
      stream->rxCopyCount() == 2 &&
      uint64_t(diag.rxPacketToStreamCopies) == 2,
    "gap-filling STREAM did not drain pending data");

  auto &rx = stream->rxStream();
  auto span = rx.span();
  ZuCHECK(span.length() == 5 && !memcmp(span.data(), "hello", 5),
    "first reordered STREAM payload mismatch");
  ZuCHECK(rx.advance(5), "first reordered STREAM advance failed");
  span = rx.span();
  ZuCHECK(span.length() == 5 && !memcmp(span.data(), "world", 5),
    "second reordered STREAM payload mismatch");
  ZuCHECK(rx.advance(5) && rx.empty(),
    "second reordered STREAM advance failed");

  auto split = client.stream(Zquic::StreamType::Bidi);
  diag = {};
  n = Zquic::FrameCodec::writeStream(
    b, sizeof(b), split->id(), 5, "world", false);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "split pending STREAM setup failed");
  ZuCHECK(split->processFrame(frame, &diag) == 0 &&
      split->rxPending() == 1 &&
      !split->rxQueued() &&
      split->rxCopyCount() == 1 &&
      uint64_t(diag.rxPacketToStreamCopies) == 1,
    "split pending STREAM state mismatch");

  n = Zquic::FrameCodec::writeStream(
    b, sizeof(b), split->id(), 0, "helloworldtails", false);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "interior-overlap STREAM setup failed");
  ZuCHECK(split->processFrame(frame, &diag) == 0 &&
      split->rxBytes() == 15 &&
      !split->rxPending() &&
      split->rxQueued() == 3 &&
      split->rxCopyCount() == 3 &&
      uint64_t(diag.rxPacketToStreamCopies) == 3,
    "interior-overlap STREAM did not copy only novel spans");

  auto &splitRx = split->rxStream();
  span = splitRx.span();
  ZuCHECK(span.length() == 5 && !memcmp(span.data(), "hello", 5),
    "split first STREAM payload mismatch");
  ZuCHECK(splitRx.advance(5), "split first STREAM advance failed");
  span = splitRx.span();
  ZuCHECK(span.length() == 5 && !memcmp(span.data(), "world", 5),
    "split second STREAM payload mismatch");
  ZuCHECK(splitRx.advance(5), "split second STREAM advance failed");
  span = splitRx.span();
  ZuCHECK(span.length() == 5 && !memcmp(span.data(), "tails", 5),
    "split third STREAM payload mismatch");
  ZuCHECK(splitRx.advance(5) && splitRx.empty(),
    "split third STREAM advance failed");
}

void testStreamTxRetention()
{
  ZuTestScope(testStreamTxRetention);

  App app;
  TestLink client{&app};
  auto stream = client.stream(Zquic::StreamType::Bidi);

  {
    auto tx = stream->txStream();
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

  App app;
  TestLink client{&app};
  auto stream = client.stream(Zquic::StreamType::Bidi);
  {
    auto tx = stream->txStream();
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
      !info.fin &&
      !stream->txRangeCount() &&
      stream->finReady(),
    "second packetized STREAM accounting mismatch");
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used) &&
      frame.offset == 3 &&
      frame.length == 3 &&
      frame.payload == "def" &&
      !frame.fin,
    "second packetized STREAM parse mismatch");

  budget = {};
  assembly = {};
  n = Zquic::StreamPacketizer::writeNext(
    b, sizeof(b), budget, assembly, *stream, &info);
  ZuCHECK(n > 0 &&
      info.offset == 6 &&
      !info.length &&
      info.fin &&
      stream->finDequeued() &&
      !stream->finReady(),
    "packetized FIN accounting mismatch");
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used) &&
      frame.offset == 6 &&
      !frame.length &&
      frame.fin,
    "packetized FIN parse mismatch");

  budget = {};
  assembly = {};
  ZuCHECK(!Zquic::StreamPacketizer::writeNext(
      b, sizeof(b), budget, assembly, *stream, &info),
    "packetizer wrote a frame for an empty stream");

  auto blocked = client.stream(Zquic::StreamType::Bidi);
  {
    auto tx = blocked->txStream();
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
    auto tx = split->txStream();
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

  uint8_t b[128];
  int n = Zquic::FrameCodec::writeStream(b, sizeof(b), 0, 0, "req", true);
  ZuCHECK(n > 0, "client STREAM frame write failed");

  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "client STREAM frame parse failed");

  Zquic::BufDiag diag;
  ZuCHECK(server.receiveFrame(frame, &diag) == 0,
    "server failed to receive peer STREAM frame");
  auto accepted = server.findStream(0);
  ZuCHECK(accepted && server.streamedCount == 1 &&
      server.lastStream == accepted &&
      accepted->processed == 1 &&
      accepted->rxComplete() &&
      accepted->finalSize() == 3 &&
      uint64_t(diag.rxPacketToStreamCopies) == 1,
    "peer stream acceptance state mismatch");

  TestLink client{&app};
  auto local = client.stream(Zquic::StreamType::Bidi);
  n = Zquic::FrameCodec::writeStream(b, sizeof(b), local->id(), 0,
    "rsp", true);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "response STREAM frame setup failed");
  ZuCHECK(client.receiveFrame(frame, &diag) == 0 &&
      !client.streamedCount &&
      local->processed == 1 &&
      local->rxComplete(),
    "existing local bidi stream receive mismatch");

  n = Zquic::FrameCodec::writeStream(b, sizeof(b), 1, 0, "bad", false);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "local-origin STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame, &diag) < 0 && !server.findStream(1),
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
  n = Zquic::FrameCodec::writeStream(b, sizeof(b), 0, 0, "a", true);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "first peer STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame) == 0 &&
      server.peerStreamsOpened(Zquic::StreamType::Bidi) == 1 &&
      server.findStream(0),
    "first peer stream under local limit did not open");

  n = Zquic::FrameCodec::writeStream(b, sizeof(b), 4, 0, "b", true);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "second peer STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame) < 0 &&
      server.peerStreamsOpened(Zquic::StreamType::Bidi) == 1 &&
      !server.findStream(4),
    "peer stream count limit was not enforced");

  server.setLocalStreamLimit(Zquic::StreamType::Bidi, 2);
  ZuCHECK(server.receiveFrame(frame) == 0 &&
      server.peerStreamsOpened(Zquic::StreamType::Bidi) == 2 &&
      server.findStream(4),
    "extended local stream count did not admit peer stream");

  server.setLocalStreamLimit(Zquic::StreamType::Uni, 1);
  n = Zquic::FrameCodec::writeStream(b, sizeof(b), 2, 0, "u", true);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "first peer uni STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame) == 0 &&
      server.peerStreamsOpened(Zquic::StreamType::Uni) == 1 &&
      server.findStream(2),
    "first peer uni stream under local limit did not open");

  n = Zquic::FrameCodec::writeStream(b, sizeof(b), 6, 0, "v", true);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "second peer uni STREAM setup failed");
  ZuCHECK(server.receiveFrame(frame) < 0 &&
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
  n = Zquic::FrameCodec::writeStream(
    b, sizeof(b), delivered->id(), 0, "hello", false);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used) &&
      delivered->receiveFrame(frame),
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
  ZuTestCall(testOutOfOrderStreamDelivery);
  ZuTestCall(testStreamTxRetention);
  ZuTestCall(testStreamPacketizer);
  ZuTestCall(testPeerStreamAcceptance);
  ZuTestCall(testStreamCountLimits);
  ZuTestCall(testResetStopFrames);
}
