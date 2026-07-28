//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiTxStream.hh>

using namespace ZuTestUtil;

namespace ZiTxStreamTest_ {

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
  struct Sent {
    ZmRef<ZiIOBuf>	buf;
    unsigned		length = 0;
    unsigned		skip = 0;
    bool		final = false;
  };
  using SentBufs =
    ZtArray<Sent, ZtArrayHeapID<"ZiTxStreamTest.Sent">>;

  SentBufs		sent;
  unsigned		allocCount = 0;

  ZmRef<ZiIOBuf> alloc(unsigned headRoom) {
    ZmRef<ZiIOBuf> buf = new StreamAlloc{};
    buf->skip = headRoom;
    buf->length = 0;
    ++allocCount;
    return buf;
  }

  void send(ZmRef<ZiIOBuf> buf, bool final) {
    sent.push(Sent{
      .buf = buf,
      .length = buf ? buf->length : 0,
      .skip = buf ? buf->skip : 0,
      .final = final
    });
  }
};

struct TestTxStream : public Zi::TxStream<TestTxStream> {
  using Base = Zi::TxStream<TestTxStream>;

  TestTxStream(
      StreamHarness &h_, unsigned maxSize, unsigned headRoom,
      unsigned tailRoom) :
    Base(maxSize, headRoom, tailRoom), h{&h_} { }
  TestTxStream(TestTxStream &&) = default;
  TestTxStream &operator =(TestTxStream &&) = default;

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    return h->alloc(headRoom);
  }

  void sendBuf_(ZmRef<ZiIOBuf> buf, bool final) {
    h->send(ZuMv(buf), final);
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
  ZuCheck(h.sent.length() == 2);
  ZuCheck(h.sent[0].length == 9);
  ZuCheck(h.sent[1].length == 9);
  ZuCheck(h.sent[0].skip == 2);
  ZuCheck(h.sent[1].skip == 2);
  ZuCheck(!h.sent[0].final);
  ZuCheck(!h.sent[1].final);

  stream << Zi::flush();

  ZuCheck(h.allocCount == 3);
  ZuCheck(h.sent.length() == 3);
  ZuCheck(h.sent[2].length == 2);
  ZuCheck(h.sent[2].skip == 2);
  ZuCheck(h.sent[2].final);
}

void testPrimitiveAppendAccounting()
{
  ZuTestScope(testPrimitiveAppendAccounting);

  StreamHarness h;
  TestTxStream stream{h, 10, 1, 1};

  stream << 'A' << 'B' << 'C';
  ZuCheck(!h.sent);

  stream << Zi::flush();
  ZuCheck(h.sent.length() == 1);
  ZuCheck(h.sent[0].length == 3);
  ZuCheck(h.sent[0].skip == 1);
  ZuCheck(h.sent[0].final);
}

void testRealPrimitiveFormatting()
{
  ZuTestScope(testRealPrimitiveFormatting);

  StreamHarness h;
  TestTxStream stream{h, 64, 1, 1};

  stream << uint64_t(1234567890123456789ULL) << ' ' << int64_t(-42) <<
    ' ' << unsigned(17) << Zi::flush();

  ZuCheck(h.sent.length() == 1);
  ZuCheck(h.sent[0].final);
  ZuCheck(h.sent[0].length == 26);
  ZuCSpan sent = h.sent[0].buf->cspan();
  ZuCheck(sent == "1234567890123456789 -42 17");
}

void testFlushElidesEmptyBuffer()
{
  ZuTestScope(testFlushElidesEmptyBuffer);

  StreamHarness h;
  TestTxStream stream{h, 10, 1, 1};

  ZuCheck(h.allocCount == 0);
  ZuCheck(!h.sent);

  stream << Zi::flush();
  ZuCheck(h.allocCount == 0);
  ZuCheck(!h.sent);

  stream << 'Z' << Zi::flush();
  ZuCheck(h.allocCount == 1);
  ZuCheck(h.sent.length() == 1);
  ZuCheck(h.sent[0].length == 1);
  ZuCheck(h.sent[0].skip == 1);
  ZuCheck(h.sent[0].final);

  stream << Zi::flush();
  ZuCheck(h.allocCount == 1);
  ZuCheck(h.sent.length() == 1);
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
  ZuCheck(!h.sent);
  ZuCheck(h.allocCount == 1);
}

void testExactCapacityAndDestruction()
{
  ZuTestScope(testExactCapacityAndDestruction);

  StreamHarness h;
  {
    TestTxStream stream{h, 6, 1, 1};
    stream << "abcd";
    ZuCheck(!h.sent);
  }
  ZuCheck(h.sent.length() == 1);
  ZuCheck(h.sent[0].length == 4);
  ZuCheck(h.sent[0].final);
}

void testMove()
{
  ZuTestScope(testMove);

  StreamHarness h1;
  {
    TestTxStream stream1{h1, 16, 1, 1};
    stream1 << "one";
    TestTxStream stream2{ZuMv(stream1)};
    ZuCheck(!h1.sent);
  }
  ZuCheck(h1.sent.length() == 1);
  ZuCheck(h1.sent[0].final);
  ZuCheck(h1.sent[0].buf->cspan() == "one");

  StreamHarness h2, h3;
  {
    TestTxStream stream2{h2, 16, 1, 1};
    TestTxStream stream3{h3, 16, 1, 1};
    stream2 << "two";
    stream3 << "three";
    stream3 = ZuMv(stream2);
    ZuCheck(h3.sent.length() == 1);
    ZuCheck(h3.sent[0].final);
    ZuCheck(h3.sent[0].buf->cspan() == "three");
  }
  ZuCheck(h2.sent.length() == 1);
  ZuCheck(h2.sent[0].final);
  ZuCheck(h2.sent[0].buf->cspan() == "two");
}

template <typename Lower>
struct TestLayer : public ZiTxLayer<TestLayer<Lower>, Lower> {
  using Base = ZiTxLayer<TestLayer<Lower>, Lower>;

  TestLayer(
      Lower &lower, unsigned reserve_, unsigned actual_, unsigned tail_) :
    Base{lower, reserve_, tail_}, reserve{reserve_}, actual{actual_} { }

  void prepareBuf_(ZiIOBuf *buf, bool final) {
    valid &= buf->skip >= reserve;
    finals.push(final);
    if (actual) {
      buf->rewind(actual);
      memset(buf->data(), int('0' + actual), actual);
    }
  }

  ZtArray<bool, ZtArrayHeapID<"ZiTxStreamTest.Finals">> finals;
  unsigned reserve = 0;
  unsigned actual = 0;
  bool valid = true;
};

void testLayerComposition()
{
  ZuTestScope(testLayerComposition);

  StreamHarness h;
  TestTxStream native{h, 32, 2, 3};
  TestLayer inner{native, 4, 2, 2};
  TestLayer outer{inner, 5, 1, 1};

  ZuCheck(outer.headRoom() == 11);
  ZuCheck(outer.tailRoom() == 6);
  outer << "payload" << Zi::flush();

  ZuCheck(h.allocCount == 1);
  ZuCheck(h.sent.length() == 1);
  ZuCheck(h.sent[0].final);
  ZuCheck(h.sent[0].skip == 8);
  ZuCheck(h.sent[0].length == 10);
  ZuCheck(inner.finals.length() == 1 && inner.finals[0]);
  ZuCheck(outer.finals.length() == 1 && outer.finals[0]);
  ZuCheck(inner.valid && outer.valid);
  ZuCheck(h.sent[0].buf->cspan() == "221payload");
}

} // namespace ZiTxStreamTest_

int main(int argc, char **argv)
{
  using namespace ZiTxStreamTest_;

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSplitAndFlush);
  ZuTestCall(testPrimitiveAppendAccounting);
  ZuTestCall(testRealPrimitiveFormatting);
  ZuTestCall(testFlushElidesEmptyBuffer);
  ZuTestCall(testOversizePrintableThrows);
  ZuTestCall(testExactCapacityAndDestruction);
  ZuTestCall(testMove);
  ZuTestCall(testLayerComposition);
  return 0;
}
