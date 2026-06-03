//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicFrame.hh>
#include <zlib/Zquic.hh>
#include <zlib/ZquicSched.hh>
#include <zlib/ZquicStream.hh>

using namespace ZuTestUtil;

struct FlowStream :
  public Zquic::Stream<FlowStream, Zquic::StreamBufAlloc<>,
    Zquic::StreamBufAlloc<>>
{
  using Base = Zquic::Stream<FlowStream, Zquic::StreamBufAlloc<>,
    Zquic::StreamBufAlloc<>>;
  FlowStream(int64_t id) : Base{id} { }
  int process(Zquic::RxStream &) { return 0; }
};

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
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(n)}, f, used) &&
    f.type == Zquic::FrameType::DataBlocked && f.value == 100,
    "DATA_BLOCKED frame mismatch");

  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 4, 200);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(n)}, f, used) &&
    f.type == Zquic::FrameType::StreamDataBlocked &&
    f.streamID == 4 && f.value == 200,
    "STREAM_DATA_BLOCKED frame mismatch");
}

void testReceiveFlowControl()
{
  ZuTestScope(testReceiveFlowControl);

  uint8_t b[128];
  Zquic::Frame frame;
  unsigned used = 0;
  Zquic::BufDiag diag;

  FlowStream stream{0};
  Zquic::ReceiveFlow flow{8, 5};
  int n = Zquic::FrameCodec::writeStream(
    b, sizeof(b), stream.id(), 3, "de", false);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "out-of-order flow STREAM setup failed");
  ZuCHECK(stream.receiveFrame(frame, flow, &diag) &&
      flow.dataUsed() == 2 &&
      flow.streamUsed() == 5 &&
      stream.rxPending() == 1 &&
      uint64_t(diag.rxPacketToStreamCopies) == 1,
    "out-of-order receive flow accounting mismatch");

  n = Zquic::FrameCodec::writeStream(
    b, sizeof(b), stream.id(), 0, "abc", false);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "gap-filling flow STREAM setup failed");
  ZuCHECK(stream.receiveFrame(frame, flow, &diag) &&
      flow.dataUsed() == 5 &&
      flow.streamUsed() == 5 &&
      stream.rxBytes() == 5 &&
      uint64_t(diag.rxPacketToStreamCopies) == 2,
    "gap-filling receive flow accounting mismatch");

  ZuCHECK(stream.receiveFrame(frame, flow, &diag) &&
      flow.dataUsed() == 5 &&
      uint64_t(diag.rxPacketToStreamCopies) == 2,
    "duplicate STREAM changed receive flow accounting");

  n = Zquic::FrameCodec::writeStream(
    b, sizeof(b), stream.id(), 5, "x", false);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "stream-limit flow violation setup failed");
  ZuCHECK(!stream.receiveFrame(frame, flow, &diag) &&
      flow.error() == Zquic::TransportError::FlowControl &&
      flow.dataUsed() == 5 &&
      flow.streamUsed() == 5,
    "stream-level flow violation was not rejected cleanly");

  FlowStream connLimited{4};
  Zquic::ReceiveFlow connFlow{6, 20};
  n = Zquic::FrameCodec::writeStream(
    b, sizeof(b), connLimited.id(), 0, "hello", false);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used) &&
      connLimited.receiveFrame(frame, connFlow, &diag),
    "connection flow first receive failed");
  n = Zquic::FrameCodec::writeStream(
    b, sizeof(b), connLimited.id(), 5, "!!", false);
  ZuCHECK(n > 0 && !Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used),
    "connection-limit flow violation setup failed");
  ZuCHECK(!connLimited.receiveFrame(frame, connFlow, &diag) &&
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
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used) &&
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
      ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)}, frame, used) &&
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

  Zquic::TxScheduler tx;
  ZuCHECK(tx.addStream(4) && tx.addControl(2) && tx.next() == 2,
    "control stream priority mismatch");
  tx.remove(2);
  ZuCHECK(tx.next() == 4, "data stream scheduling mismatch");

  Zquic::PacketBudget budget;
  budget.pmtu = 50;
  budget.congestion = 50;
  budget.antiAmplification = 50;
  Zquic::PacketAssembly packet;
  ZuCHECK(packet.addControl(budget, 10) &&
    packet.addStream(budget, 20) &&
    !packet.addStream(budget, 1) &&
    packet.addControl(budget, 20) &&
    packet.controlFrames() == 2 && packet.streamAdded() &&
    !budget.remaining(), "packet assembly budget/STREAM rules mismatch");
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
  ZuTestCall(testReceiveFlowControl);
  ZuTestCall(testReceiveFlowUpdates);
  ZuTestCall(testScheduling);
  ZuTestCall(testPacing);
}
