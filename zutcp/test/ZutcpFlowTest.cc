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
  auto data = dataFrame(buf, 0);
  ZuCHECK(cxn->processFrame(data, buf) == 0 && cxn->rxCreditUsed() == 12 &&
      cxn->rxCreditLimit() == 16, "connection byte window setup");
  auto extra = packet(app, "abcde");
  data = dataFrame(extra, 12);
  ZuCHECK(!cxn->receiveDataFrame(data, extra) && cxn->rxCreditUsed() == 12 &&
      cxn->rxAdmitted() == 12, "over-window bytes preserve admitted state");
  ZuCHECK(consume(cxn->reader(), "abcdefgh") && cxn->retireRx(8) &&
      cxn->rxCreditLimit() == 24 && cxn->rxRetired() == 8,
    "retiring reader bytes replenishes the receive right edge once");
  cxn->txCredit(4);
  cxn->consumeTxCredit(4);
  cxn->extendTxCredit(9);
  ZuCHECK(cxn->txCreditAvailable() == 5, "extended byte credit becomes available");
  cxn->extendTxCredit(3);
  ZuCHECK(cxn->txCreditAvailable() == 5, "older credit cannot retract an extension");
  cxn->closeRx();
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
  ZuTestCall(testPacing);
}
