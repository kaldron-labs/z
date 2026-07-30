//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiTx.hh>

using namespace ZuTestUtil;

namespace {

using TxAlloc = ZiTxBufAlloc<32, 128>;

struct FakeConnection {
  ZiIOContext	io;
  unsigned	sendCalls = 0;

  void send(ZiIOFn fn)
  {
    ++sendCalls;
    fn(io);
  }

  FakeConnection *mx() { return this; }

  template <typename L>
  void txInvoke(L &&l)
  {
    ZuFwd<L>(l)();
  }
};

struct TxHarness : public FakeConnection, public ZiTx<TxHarness> {
  using ZiConnection = FakeConnection;

  unsigned	sentCount = 0;
  unsigned	abortedCount = 0;
  char		sentOrder[16] = {};
  bool		sentOK[16] = {};
  char		abortedOrder[16] = {};
  bool		abortedOK[16] = {};

  FakeConnection *cxn() { return this; }

  void sent(ZmRef<ZiTxBuf> buf, bool ok)
  {
    if (sentCount < 16) {
      sentOrder[sentCount] = char(buf->ptr<const uint8_t>()[0]);
      sentOK[sentCount] = ok;
    }
    ++sentCount;
  }

  void aborted(ZmRef<ZiTxBuf> buf, bool ok)
  {
    if (abortedCount < 16) {
      abortedOrder[abortedCount] = char(buf->ptr<const uint8_t>()[0]);
      abortedOK[abortedCount] = ok;
    }
    ++abortedCount;
  }

  void step(int length)
  {
    io.length = length;
    io();
  }

  void queue(ZmRef<ZiTxBuf> buf)
  {
    ZiTx<TxHarness>::send(ZuMv(buf));
  }
};

ZmRef<ZiTxBuf> mkBuf(char c, unsigned n)
{
  ZmRef<ZiTxBuf> buf = new TxAlloc{};
  for (unsigned i = 0; i < n; ++i)
    buf->append(ZuSpan{&c, 1});
  return buf;
}

void testFifoAndPartialSendContinuation()
{
  ZuTestScope(testFifoAndPartialSendContinuation);

  TxHarness tx;

  auto a = mkBuf('A', 3);
  auto b = mkBuf('B', 3);
  auto c = mkBuf('C', 3);

  tx.queue(a);
  tx.queue(b);
  tx.queue(c);

  ZuCheck(tx.txQueue.count_() == 3);
  ZuCheck(tx.sendCalls == 3);

  tx.step(1);
  ZuCheck(tx.sentCount == 0);
  ZuCheck(tx.io.offset == 1);

  tx.step(int(tx.io.size - tx.io.offset));
  ZuCheck(tx.sentCount == 1);
  ZuCheck(tx.sentOrder[0] == 'A');
  ZuCheck(tx.sentOK[0]);
  ZuCheck(tx.io.size == 3);

  tx.step(int(tx.io.size - tx.io.offset));
  ZuCheck(tx.sentCount == 2);
  ZuCheck(tx.sentOrder[1] == 'B');
  ZuCheck(tx.sentOK[1]);
  ZuCheck(tx.io.size == 3);

  tx.step(int(tx.io.size - tx.io.offset));
  ZuCheck(tx.sentCount == 3);
  ZuCheck(tx.sentOrder[2] == 'C');
  ZuCheck(tx.sentOK[2]);

  ZuCheck(tx.txQueue.count_() == 0);
  ZuCheck(tx.io.completed());
}

void testSendFailureDrainsQueue()
{
  ZuTestScope(testSendFailureDrainsQueue);

  TxHarness tx;

  auto a = mkBuf('A', 3);
  auto b = mkBuf('B', 3);

  tx.queue(a);
  tx.queue(b);
  ZuCheck(tx.txQueue.count_() == 2);

  tx.step(-1);

  ZuCheck(tx.sentCount == 1);
  ZuCheck(tx.sentOrder[0] == 'A');
  ZuCheck(!tx.sentOK[0]);
  ZuCheck(tx.txQueue.count_() == 0);
  ZuCheck(tx.io.completed());
}

void testAbortQueuedAndHeadCases()
{
  ZuTestScope(testAbortQueuedAndHeadCases);

  {
    TxHarness tx;

    auto a = mkBuf('A', 3);
    auto b = mkBuf('B', 3);
    auto c = mkBuf('C', 3);

    tx.queue(a);
    tx.queue(b);
    tx.queue(c);

    tx.abort(c);
    ZuCheck(tx.abortedCount == 1);
    ZuCheck(tx.abortedOrder[0] == 'C');
    ZuCheck(tx.abortedOK[0]);
    ZuCheck(tx.txQueue.count_() == 2);

    tx.step(int(tx.io.size - tx.io.offset));
    tx.step(int(tx.io.size - tx.io.offset));

    ZuCheck(tx.sentCount == 2);
    ZuCheck(tx.sentOrder[0] == 'A');
    ZuCheck(tx.sentOrder[1] == 'B');
  }

  {
    TxHarness tx;

    auto a = mkBuf('A', 3);
    auto b = mkBuf('B', 3);

    tx.queue(a);
    tx.queue(b);

    tx.abort(a);
    ZuCheck(tx.abortedCount == 1);
    ZuCheck(tx.abortedOrder[0] == 'A');
    ZuCheck(!tx.abortedOK[0]);

    tx.step(int(tx.io.size - tx.io.offset));
    tx.step(int(tx.io.size - tx.io.offset));

    ZuCheck(tx.sentCount == 2);
    ZuCheck(tx.sentOrder[0] == 'A');
    ZuCheck(tx.sentOrder[1] == 'B');
  }
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testFifoAndPartialSendContinuation);
  ZuTestCall(testSendFailureDrainsQueue);
  ZuTestCall(testAbortQueuedAndHeadCases);
  return 0;
}
