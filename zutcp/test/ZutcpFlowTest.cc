//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

#include "ZutcpConnectionFixture.hh"

void testFlowCredit()
{
  ZuTestScope(testFlowCredit);
  Zutcp::FlowCredit credit{100};
  ZuCHECK(credit.consume(60) && credit.allows(40) && !credit.allows(41) &&
    credit.shouldUpdate() && !credit.consume(41), "byte-window boundary mismatch");
  credit.extend(200);
  ZuCHECK(credit.consume(140) && credit.blocked(), "extended window mismatch");
  credit.extend(300);
  ZuCHECK(credit.consumeTo(250) && credit.used() == 250 &&
    credit.consumeTo(200) && credit.used() == 250,
    "duplicate or reordered input must preserve the high-water mark");
}

void testConnectionCredit()
{
  ZuTestScope(testConnectionCredit);
  TestApp app;
  ZmRef<TestConnection> cxn = new TestConnection{&app};
  auto buf = packet(app, "abcdefghijkl");
  auto frame = dataFrame(buf, 0);
  ZuCHECK(cxn->processFrame(frame, buf) == 0 && cxn->rxCreditUsed() == 12 &&
    cxn->rxCreditLimit() == 16, "connection window setup failed");
  auto extra = packet(app, "abcde");
  frame = dataFrame(extra, 12);
  ZuCHECK(!cxn->receiveDataFrame(frame, extra) && cxn->rxCreditUsed() == 12 &&
    cxn->rxAdmitted() == 12, "over-window input must preserve admitted state");
  frame = {};
  frame.type = Zutcp::FrameType::DataBlocked;
  frame.value = 16;
  ZuCHECK(cxn->receiveDataBlocked(frame) && cxn->rxCreditUsed() == 12 &&
    cxn->rxCreditLimit() == 16, "blocked advice must not grant receive credit");
  ZuCHECK(consume(cxn->reader(), "abcdefgh") && cxn->retireRx(8) &&
    cxn->rxCreditLimit() == 24 && cxn->rxRetired() == 8 &&
    cxn->queuedControlFrames() == 1,
    "retired bytes must replenish the connection window once");
  cxn->txCredit(4);
  cxn->consumeTxCredit(4);
  frame = {};
  frame.type = Zutcp::FrameType::MaxData;
  frame.value = 9;
  ZuCHECK(cxn->applyMaxData(frame) && cxn->txCreditAvailable() == 5,
    "peer credit must extend the same transmit window");
  frame.value = 3;
  ZuCHECK(cxn->applyMaxData(frame) && cxn->txCreditAvailable() == 5,
    "stale credit must not shrink the window");
  cxn->closeRx();
}

void testControlFrames()
{
  ZuTestScope(testControlFrames);
  uint8_t bytes[64];
  Zutcp::Frame frame;
  unsigned used = 0;
  auto control = Zutcp::ControlFrame::blocked(1024);
  int n = control.write(bytes, sizeof(bytes));
  ZuCHECK(n > 0 && !Zutcp::FrameCodec::parse({bytes, unsigned(n)}, frame, used) &&
    frame.type == Zutcp::FrameType::DataBlocked && frame.value == 1024,
    "blocked control must encode only the connection limit");
  control = Zutcp::ControlFrame::flowUpdate({Zutcp::FrameType::MaxData, 2048});
  n = control.write(bytes, sizeof(bytes));
  ZuCHECK(n > 0 && !Zutcp::FrameCodec::parse({bytes, unsigned(n)}, frame, used) &&
    frame.type == Zutcp::FrameType::MaxData && frame.value == 2048,
    "credit update must encode only the connection window");
  TestApp app;
  ZmRef<TestConnection> cxn = new TestConnection{&app};
  const uint8_t excluded[] = {0x11, 0x12, 0x13, 0x15, 0x16, 0x17};
  bool rejected = true;
  for (auto code : excluded) {
    bytes[0] = code;
    bytes[1] = bytes[2] = 0;
    rejected &= !Zutcp::FrameCodec::parse({bytes, 3}, frame, used) &&
      frame.type == Zutcp::FrameType::Unknown &&
      !cxn->frameLegal(Zutcp::PktNumSpace::AppData, frame);
  }
  ZuCHECK(rejected, "removed stream/count wire records must be unsupported");
}

void testPacing()
{
  ZuTestScope(testPacing);

  Zutcp::Pacer pacer;
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
  ZuTestCall(testFlowCredit);
  ZuTestCall(testConnectionCredit);
  ZuTestCall(testControlFrames);
  ZuTestCall(testPacing);
}
