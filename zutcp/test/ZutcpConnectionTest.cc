//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"

using namespace ZuTestUtil;

#include "ZutcpConnectionFixture.hh"

void testReorderedRx()
{
  ZuTestScope(testReorderedRx);
  TestApp app;
  ZmRef<TestConnection> cxn = new TestConnection{&app};
  auto tail = packet(app, "def");
  auto frame = dataFrame(tail, 3, true);
  ZuCHECK(cxn->processFrame(frame, tail) == 0 && cxn->rxBytes() == 0 &&
    cxn->rxPending() == 1, "out-of-order bytes must wait for their prefix");
  auto head = packet(app, "abc");
  frame = dataFrame(head, 0);
  ZuCHECK(cxn->processFrame(frame, head) == 0 && cxn->rxBytes() == 6 &&
    cxn->rxCreditUsed() == 6 && cxn->rxAdmitted() == 6,
    "the connection must deliver and charge each byte once");
  ZuCHECK(consume(cxn->reader(), "ab") && cxn->retireRx(2) &&
    cxn->rxRetired() == 2, "partial delivery must retire only consumed bytes");
  ZuCHECK(consume(cxn->reader(), "cdef") && cxn->retireRx(4) &&
    cxn->rxComplete() && cxn->finalSize() == 6,
    "delivery across packet slices must reach the final byte");
  frame = dataFrame(tail, 3, true);
  ZuCHECK(cxn->processFrame(frame, tail) == 0 && cxn->rxCreditUsed() == 6 &&
    cxn->rxAdmitted() == 6 && cxn->reader().empty(),
    "a duplicate must not enqueue or charge bytes again");
  cxn->closeRx();
}

void testRetainedTx()
{
  ZuTestScope(testRetainedTx);
  TestApp app;
  ZmRef<TestConnection> cxn = new TestConnection{&app};
  cxn->txCredit(4);
  cxn->txQueueMax(1);
  auto buf = ZutcpTest::plaintextAllocator<Zutcp::TxBufAlloc<>>(app.mx())->allocBuf();
  memcpy(buf->data_(), "abcdef", 6);
  buf->length = 6;
  ZuCHECK(cxn->enqueue_(buf) && !cxn->enqueue_(buf) && cxn->txQueueCount() == 1,
    "queue pressure must reject without duplicating retained ownership");
  cxn->fin();
  Zutcp::TxRange original;
  ZuCHECK(cxn->txRange(0, original) && original.length == 6 &&
      original.storage->pending == buf && cxn->txCreditAvailable() == 4 &&
      cxn->txBufferedBytes() == 6,
    "window pressure must leave the whole original queued without preparing a prefix");
  cxn->extendTxCredit(6);
  Zutcp::TxRange committed;
  ZuCHECK(cxn->commitTxRange(committed, original, original.length) &&
      cxn->consumeTxCredit(committed.length) && !cxn->txQueueCount() &&
      !cxn->txBufferedBytes() && committed.storage->pending == buf,
    "whole-original commitment releases its admission while retaining its backing");
  ZuCHECK(cxn->recordTxUnackd(0, 6, false) && cxn->retry(committed, false) &&
      !cxn->txCreditAvailable() && cxn->txUnackdBytes() == 6,
    "recovery retains original bytes without consuming credit again");
  uint64_t fin = 0;
  ZuCHECK(cxn->dequeueFin(fin) && fin == 6 && cxn->recordTxUnackd(fin, 0, true) &&
      cxn->txUnackdBytes() == 7,
    "FIN consumes its own sequence position independently of application byte credit");
  ZuCHECK(cxn->ackTxUnackd(0, 4) && !cxn->txCompleted() &&
      cxn->ackTxUnackd(4, 2, true) && cxn->txCompleted() && cxn->txDrained(),
    "partial and final acknowledgments retire exactly their byte/FIN ranges");
}

void testRuntimeReset()
{
  ZuTestScope(testRuntimeReset);
  TestApp app;
  ZmRef<TestConnection> cxn = new TestConnection{&app};
  auto first = packet(app, "abc");
  auto frame = dataFrame(first, 0);
  auto gap = packet(app, "ghi");
  ZuCHECK(cxn->processFrame(frame, first) == 0 &&
    cxn->processFrame(dataFrame(gap, 6, true), gap) == 0 &&
    cxn->rxBytes() == 3 && cxn->rxPending() == 1,
    "reset setup must retain delivered bytes and an out-of-order final range");
  cxn->txCredit(16);
  auto tx = packet(app, "old");
  ZuCHECK(cxn->enqueue_(tx) && cxn->recordTxUnackd(0, 2, false),
    "reset setup must retain both producer and unacknowledged ranges");
  cxn->fin();
  auto generation = cxn->dataGen();
  cxn->resetRuntime();
  ZuCHECK(cxn->dataGen() != generation && !cxn->rxBytes() && !cxn->rxPending() &&
    cxn->reader().empty() && !cxn->rxAdmitted() && !cxn->rxRetired() &&
    !cxn->finReceived() && !cxn->resetReceived() && !cxn->stopReceived() &&
    cxn->rxError() == Zutcp::DataError::None,
    "reset must clear the old Rx extent, slices, final state and errors");
  ZuCHECK(!cxn->txBytes() && !cxn->txBufferedBytes() && !cxn->txRangeCount() &&
    !cxn->txQueueCount() && !cxn->txUnackdCount() && !cxn->finSent() &&
    !cxn->txCompleted() && !cxn->resetSent() && !cxn->stopSent() &&
    cxn->txError() == Zutcp::DataError::None,
    "reset must release old Tx ranges and clear FIN/control/error state");
  auto fresh = packet(app, "new");
  ZuCHECK(cxn->processFrame(dataFrame(fresh, 0, true), fresh) == 0 &&
    cxn->rxBytes() == 3 && cxn->rxComplete() &&
    consume(cxn->reader(), "new") && cxn->retireRx(3),
    "reused Rx must accept offset zero after the previous queue advanced");
  cxn->txCredit(16);
  ZuCHECK(cxn->enqueue_(fresh), "reused Tx must accept a new source range");
  Zutcp::TxRange range;
  bool fin = false;
  Zutcp::PktBudget budget;
  ZuCHECK(cxn->nextTxRange(budget, range, fin) && !range.dataOffset &&
    range.storage->pending == fresh && range.length == 3 && !fin,
    "reused Tx must restart at zero with the new backing");
  cxn->reset(17);
  cxn->stop(23);
  cxn->resetRuntime();
  ZuCHECK(!cxn->resetReceived() && !cxn->stopReceived() && !cxn->stopSent() &&
    !cxn->resetSent() && !cxn->txCompleted() &&
    cxn->rxError() == Zutcp::DataError::None && !cxn->rxAppError() &&
    cxn->txError() == Zutcp::DataError::None && !cxn->txAppError(),
    "reuse must clear terminal flags and error causes in both directions");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testReorderedRx);
  ZuTestCall(testRetainedTx);
  ZuTestCall(testRuntimeReset);
}
