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

using StreamAlloc = Zi::IOBufAlloc<ZiIOBuf, 64, 256, ZuStringT<"ZiTxStreamTest.Buf">>;

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
  Zi::RxRange		range;
  unsigned		allocCount = 0;
  bool			sendOK = true;
  unsigned		rejectAt = 0;

  ZmRef<ZiIOBuf> alloc(unsigned headRoom) {
    // Model lower-layer admission rejection, not Z heap allocation failure.
    if (++allocCount == rejectAt) return nullptr;
    ZmRef<ZiIOBuf> buf = new StreamAlloc{nullptr, 0};
    buf->skip = headRoom;
    buf->length = 0;
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

  ~TestTxStream() { this->flush(); }

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    return h->alloc(headRoom);
  }

  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool final) {
    h->send(ZuMv(buf), final);
    return h->sendOK;
  }

  bool sendRange_(Zi::RxRange range) {
    h->range = ZuMv(range);
    return h->sendOK;
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

void testRetainedPhysicalRange()
{
  ZuTestScope(testRetainedPhysicalRange);
  ZmRef<ZiIOBuf> source = new StreamAlloc{nullptr, 0};
  *source << "..abcdef";
  source->advance(2);
  Zi::RxRange range{source, 1, 3};
  source->advance(3);
  source->length = 1;
  auto base = source->data_();
  auto size = source->size;

  StreamHarness h;
  TestTxStream stream{h, 12, 2, 1};
  stream << "H";
  ZuCheck(stream.appendRange(range));
  ZuCheck(h.allocCount == 1 && h.sent.length() == 1 && !h.sent[0].final);
  ZuCheck(h.sent[0].buf->cspan() == "H");
  ZuCheck(h.range.buf == source && h.range.offset == 3 && h.range.cspan() == "bcd");
  ZuCheck(source->data_() == base && source->size == size &&
    source->skip == 5 && source->length == 1);
  ZuCheck(!stream.appendRange(Zi::RxRange::fromStorage(source, size, 1)));
  ZuCheck(stream.failed() && !stream.flush());

  StreamHarness logical;
  TestTxStream next{logical, 12, 2, 1};
  ZuCheck(next.appendRange(source, 0, 1));
  ZuCheck(!logical.allocCount && logical.range.offset == 5 && logical.range.cspan() == "d");
}

void testAdoptedBuffers()
{
  ZuTestScopeRT(testAdoptedBuffers);
  StreamHarness direct;
  TestTxStream same{direct, 12, 2, 1};
  ZmRef<ZiIOBuf> small = new StreamAlloc{nullptr, 0};
  small->skip = 3;
  small->append(ZuBSpan{"small"});
  same << small;
  ZuCheckRT(same.flush() && direct.sent.length() == 1);
  ZuCheckRT(direct.sent[0].buf.ptr() == small.ptr() && direct.sent[0].skip == 3);
  ZuCheckRT(!direct.allocCount);

  StreamHarness split;
  TestTxStream frames{split, 12, 2, 1};
  ZmRef<ZiIOBuf> large = new StreamAlloc{nullptr, 0};
  large->skip = 7;
  large->append(ZuBSpan{"abcdefghijklmnopqrst"});
  frames << large;
  ZuCheckRT(frames.flush() && split.sent.length() == 3);
  ZtString<> joined;
  for (const auto &sent : split.sent) {
    ZuCheckRT(sent.length <= 9 && sent.skip >= 2 && sent.buf->avail() >= 1);
    joined << sent.buf->cspan();
  }
  ZuCheckRT(joined == "abcdefghijklmnopqrst" && large->skip == 7 && large->length == 20);

  StreamHarness geometry;
  TestTxStream adjusted{geometry, 12, 2, 1};
  ZmRef<ZiIOBuf> noRoom = new StreamAlloc{nullptr, 0};
  noRoom->append(ZuBSpan{"space"});
  noRoom->size = noRoom->length; // readable storage has no head or tail space
  adjusted << noRoom;
  ZuCheckRT(adjusted.flush() && geometry.sent.length() == 1);
  ZuCheckRT(geometry.sent[0].buf.ptr() != noRoom.ptr() && geometry.sent[0].skip == 2);
  ZuCheckRT(geometry.sent[0].buf->cspan() == "space");

  StreamHarness refused;
  refused.rejectAt = 2;
  TestTxStream rejected{refused, 12, 2, 1};
  rejected << large;
  ZuCheckRT(rejected.failed() && !rejected.flush() && refused.sent.length() == 1);
  ZuCheckRT(refused.sent[0].buf->cspan() == "abcdefghi");
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

struct Delegate {
  unsigned *calls;
  template <typename S> void print(S &s) const {
    ++*calls;
    s << "abcdefghijklmnopqrst";
  }
  friend ZuPrintFn ZuPrintType(Delegate *);
};

void testOversizePrintableFails()
{
  ZuTestScope(testOversizePrintableFails);

  StreamHarness h;
  TestTxStream empty{h, 12, 2, 1};
  empty << BigPrint{32};
  ZuCheck(empty.failed());
  ZuCheck(!empty.flush());
  ZuCheck(!h.allocCount && !h.sent);

  {
    TestTxStream prefix{h, 12, 2, 1};
    prefix << "abc" << BigPrint{32} << "ignored";
    ZuCheck(prefix.failed());
    ZuCheck(!h.sent);
    ZuCheck(!prefix.flush());
    ZuCheck(h.sent.length() == 1);
    ZuCheck(h.sent[0].buf->cspan() == "abc");
    ZuCheck(h.sent[0].final);
    ZuCheck(!prefix.flush());
  }
  ZuCheck(h.sent.length() == 1);

  StreamHarness d;
  {
    TestTxStream prefix{d, 12, 2, 1};
    prefix << "prefix" << BigPrint{32};
  }
  ZuCheck(d.sent.length() == 1);
  ZuCheck(d.sent[0].buf->cspan() == "prefix");
}

void testDelegateAndAdmission()
{
  ZuTestScope(testDelegateAndAdmission);

  StreamHarness h;
  unsigned calls = 0;
  TestTxStream stream{h, 12, 2, 1};
  stream << Delegate{&calls};
  ZuCheck(stream.flush());
  ZuCheck(calls == 1 && h.sent.length() == 3);
  ZuCheck(h.sent[0].length == 9 && h.sent[1].length == 9);
  ZuCheck(h.sent[2].length == 2);

  StreamHarness initial;
  initial.rejectAt = 1;
  TestTxStream noBuf{initial, 12, 2, 1};
  noBuf << "a" << Delegate{&calls};
  ZuCheck(noBuf.failed() && !noBuf.flush());
  ZuCheck(initial.allocCount == 1 && !initial.sent);
  ZuCheck(calls == 1);

  StreamHarness rollover;
  rollover.rejectAt = 2;
  TestTxStream noNext{rollover, 12, 2, 1};
  noNext << Delegate{&calls} << "ignored";
  ZuCheck(noNext.failed() && !noNext.flush());
  ZuCheck(rollover.allocCount == 2 && rollover.sent.length() == 1);
  ZuCheck(rollover.sent[0].buf->cspan() == "abcdefghi");
}

void testMoveFailure()
{
  ZuTestScope(testMoveFailure);

  StreamHarness src, dest;
  TestTxStream from{src, 12, 2, 1};
  TestTxStream to{dest, 12, 2, 1};
  from << "source";
  to << "dest";
  dest.sendOK = false;
  to = ZuMv(from);
  ZuCheck(to.failed());
  ZuCheck(!to.flush());
  ZuCheck(!from.failed() && from.flush());
  ZuCheck(dest.sent.length() == 1 && !src.sent);

  StreamHarness h;
  TestTxStream prefix{h, 12, 2, 1};
  prefix << "abc" << BigPrint{32};
  TestTxStream moved{ZuMv(prefix)};
  ZuCheck(moved.failed() && !prefix.failed());
  ZuCheck(!moved.flush());
  ZuCheck(h.sent.length() == 1 && h.sent[0].buf->cspan() == "abc");
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

void testFailureState()
{
  ZuTestScope(testFailureState);

  StreamHarness h;
  h.sendOK = false;
  TestTxStream stream{h, 6, 1, 1};

  stream << "abcdefgh";
  ZuCheck(stream.failed());
  ZuCheck(!stream);
  ZuCheck(h.allocCount == 1);
  ZuCheck(h.sent.length() == 1);
  ZuCheck(h.sent[0].length == 4);
  ZuCheck(!h.sent[0].final);

  stream << "ignored" << Zi::flush();
  ZuCheck(h.allocCount == 1);
  ZuCheck(h.sent.length() == 1);

  StreamHarness flushHarness;
  flushHarness.sendOK = false;
  TestTxStream flushStream{flushHarness, 16, 1, 1};
  flushStream << "flush" << Zi::flush();
  ZuCheck(flushStream.failed());
  ZuCheck(flushHarness.sent.length() == 1);
  ZuCheck(flushHarness.sent[0].final);
}

template <typename Lower>
struct TestLayer : public ZiTxLayer<TestLayer<Lower>, Lower> {
  using Base = ZiTxLayer<TestLayer<Lower>, Lower>;

  TestLayer(
      Lower &lower, unsigned reserve_, unsigned actual_, unsigned tail_) :
    Base{lower, reserve_, tail_}, reserve{reserve_}, actual{actual_} { }

  ~TestLayer() { this->flush(); }

  bool prepareBuf_(ZiIOBuf *buf, bool final) {
    if (!prepareOK) return false;
    valid &= buf->skip >= reserve;
    finals.push(final);
    if (actual) {
      buf->rewind(actual);
      memset(buf->data(), int('0' + actual), actual);
    }
    return true;
  }

  ZtArray<bool, ZtArrayHeapID<"ZiTxStreamTest.Finals">> finals;
  unsigned reserve = 0;
  unsigned actual = 0;
  bool valid = true;
  bool prepareOK = true;
};

// Model a lower transform that can reclaim surplus source headroom for its
// final trailer. Upper framing must still have its own writable trailer room.
struct ShiftTxStream : Zi::TxStream<ShiftTxStream> {
  StreamHarness &h;

  ShiftTxStream(StreamHarness &h_) : Zi::TxStream<ShiftTxStream>{64, 2, 4}, h{h_} { }
  ~ShiftTxStream() { this->flush(); }
  ZmRef<ZiIOBuf> allocBuf_(unsigned head) { return h.alloc(head); }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool final) {
    h.send(ZuMv(buf), final); return true;
  }
  bool canAdopt_(const ZiIOBuf &buf, unsigned head = 0, unsigned tail = 0) const {
    uint64_t length = uint64_t(buf.length) + head + tail;
    return head <= buf.skip && tail <= buf.avail() && buf.skip - head >= 2 &&
      length <= 58 && uint64_t(buf.size) >= length + 6;
  }
};

void testLayerAdoption()
{
  ZuTestScope(testLayerAdoption);
  StreamHarness h;
  ShiftTxStream lower{h};
  TestLayer layer{lower, 4, 4, 0};
  auto buf = h.alloc(10);
  *buf << "payload123";
  buf->size = buf->skip + buf->length;
  auto identity = buf.ptr();
  ZuCheck(!buf->avail());
  ZuCheck(layer.canAdopt_(*buf));
  {
    TestLayer trailer{lower, 4, 4, 2};
    ZuCheck(!trailer.canAdopt_(*buf));
  }
  layer << ZuMv(buf) << Zi::flush();
  ZuCheck(h.allocCount == 1 && h.sent.length() == 1);
  ZuCheck(h.sent[0].buf.ptr() == identity);
  ZuCheck(h.sent[0].buf->cspan() == "4444payload123");
}

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


void testLayerFailure()
{
  ZuTestScope(testLayerFailure);

  StreamHarness h;
  TestTxStream native{h, 32, 2, 3};
  TestLayer inner{native, 4, 2, 2};
  TestLayer outer{inner, 5, 1, 1};
  inner.prepareOK = false;
  outer << "payload";
  ZuCheck(!outer.flush());
  ZuCheck(outer.failed() && inner.failed());
  ZuCheck(!native.failed() && !h.sent);
  outer << "ignored";
  ZuCheck(!outer.flush() && h.allocCount == 1);

  StreamHarness reject;
  reject.sendOK = false;
  TestTxStream below{reject, 32, 2, 3};
  TestLayer above{below, 4, 2, 2};
  above << "payload";
  ZuCheck(!above.flush());
  ZuCheck(above.failed() && below.failed());
  ZuCheck(reject.sent.length() == 1);
}

} // namespace ZiTxStreamTest_

int main(int argc, char **argv)
{
  using namespace ZiTxStreamTest_;

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSplitAndFlush);
  ZuTestCall(testRetainedPhysicalRange);
  ZuTestCall(testAdoptedBuffers);
  ZuTestCall(testPrimitiveAppendAccounting);
  ZuTestCall(testRealPrimitiveFormatting);
  ZuTestCall(testFlushElidesEmptyBuffer);
  ZuTestCall(testOversizePrintableFails);
  ZuTestCall(testDelegateAndAdmission);
  ZuTestCall(testMoveFailure);
  ZuTestCall(testLayerFailure);
  ZuTestCall(testExactCapacityAndDestruction);
  ZuTestCall(testMove);
  ZuTestCall(testFailureState);
  ZuTestCall(testLayerComposition);
  ZuTestCall(testLayerAdoption);
  return 0;
}
