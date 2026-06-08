//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiTxStream.hh>

using namespace ZuTestUtil;

namespace {

using StreamAlloc = ZiIOBufAlloc<64, 256, "ZiTxStreamTest.Buf">;

struct BigPrint_Print;
struct BigPrint {
  unsigned n;
  friend struct BigPrint_Print;
  friend BigPrint_Print ZuPrintType(BigPrint *);
};
struct BigPrint_Print : public ZuPrintBuffer {
  static unsigned length(const BigPrint &v) { return v.n; }
  static unsigned print(char *buf, unsigned n, const BigPrint &) {
    for (unsigned i = 0; i < n; ++i) buf[i] = 'X';
    return n;
  }
};
BigPrint_Print ZuPrintType(BigPrint *);

struct StreamHarness {
  unsigned		allocCount = 0;
  unsigned		sendCount = 0;

  ZmRef<ZiIOBuf>	sent[16];
  unsigned		sentLen[16] = {};
  unsigned		sentSkip[16] = {};

  ZmRef<ZiIOBuf> alloc(unsigned headRoom) {
    ZmRef<ZiIOBuf> buf = new StreamAlloc{};
    buf->skip = headRoom;
    buf->length = 0;
    ++allocCount;
    return buf;
  }

  void send(ZmRef<ZiIOBuf> buf) {
    if (sendCount < 16) {
      sent[sendCount] = buf;
      sentLen[sendCount] = buf ? buf->length : 0;
      sentSkip[sendCount] = buf ? buf->skip : 0;
    }
    ++sendCount;
  }
};

struct TestTxStream : public Zi::TxStream<TestTxStream> {
  using Base = Zi::TxStream<TestTxStream>;

  TestTxStream(
      StreamHarness &h_, unsigned maxSize, unsigned headRoom,
      unsigned tailRoom) :
    Base(maxSize, headRoom, tailRoom), h{&h_} { }

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    return h->alloc(headRoom);
  }

  void sendBuf_(ZmRef<ZiIOBuf> buf) {
    h->send(ZuMv(buf));
  }

  StreamHarness	*h;
};

void testSplitAndFlush()
{
  ZuTestScope(testSplitAndFlush);

  StreamHarness h;
  TestTxStream stream{h, 12, 2, 1};

  char payload[20];
  for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = 'a' + i;

  stream << ZuCSpan(payload, sizeof(payload));

  ZuCheck(h.allocCount == 3);
  ZuCheck(h.sendCount == 2);
  ZuCheck(h.sentLen[0] == 9);
  ZuCheck(h.sentLen[1] == 9);
  ZuCheck(h.sentSkip[0] == 2);
  ZuCheck(h.sentSkip[1] == 2);

  stream << Zi::flush();

  ZuCheck(h.allocCount == 3);
  ZuCheck(h.sendCount == 3);
  ZuCheck(h.sentLen[2] == 2);
  ZuCheck(h.sentSkip[2] == 2);
}

void testPrimitiveAppendAccounting()
{
  ZuTestScope(testPrimitiveAppendAccounting);

  StreamHarness h;
  TestTxStream stream{h, 10, 1, 1};

  stream << 'A' << 'B' << 'C';
  ZuCheck(h.sendCount == 0);

  stream << Zi::flush();
  ZuCheck(h.sendCount == 1);
  ZuCheck(h.sentLen[0] == 3);
  ZuCheck(h.sentSkip[0] == 1);
}

void testFlushElidesEmptyBuffer()
{
  ZuTestScope(testFlushElidesEmptyBuffer);

  StreamHarness h;
  TestTxStream stream{h, 10, 1, 1};

  ZuCheck(h.allocCount == 0);
  ZuCheck(h.sendCount == 0);

  stream << Zi::flush();
  ZuCheck(h.allocCount == 0);
  ZuCheck(h.sendCount == 0);

  stream << 'Z' << Zi::flush();
  ZuCheck(h.allocCount == 1);
  ZuCheck(h.sendCount == 1);
  ZuCheck(h.sentLen[0] == 1);
  ZuCheck(h.sentSkip[0] == 1);

  stream << Zi::flush();
  ZuCheck(h.allocCount == 1);
  ZuCheck(h.sendCount == 1);
}

void testOversizePrintableThrows()
{
  ZuTestScope(testOversizePrintableThrows);

  StreamHarness h;
  TestTxStream stream{h, 12, 2, 1};

  bool threw = false;
  try {
    stream << BigPrint{32};
  } catch (const ZeException &) {
    threw = true;
  }

  ZuCheck(threw);
  ZuCheck(h.sendCount == 1);
  ZuCheck(h.allocCount == 2);
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSplitAndFlush);
  ZuTestCall(testPrimitiveAppendAccounting);
  ZuTestCall(testFlushElidesEmptyBuffer);
  ZuTestCall(testOversizePrintableThrows);
  return 0;
}
