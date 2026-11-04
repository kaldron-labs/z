//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZfixRxFramer.hh>

using namespace ZuTestUtil;
using namespace Zfix;

constexpr auto Wire = "8=FIX.4.4\1" "9=13\1" "35=D\1" "58=ABCD\1" "10=155\1"_z;
using Queue = ZiRxQueue;
using Stream = ZiRxStream<Queue>;

using Buffer = Zi::IOBufAlloc<Queue::Node, ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.RxFramerTest">>;
static auto alloc() { return ZmRef<Queue::Node>{new Buffer{nullptr, 0}}; }
static void push(Stream &stream, ZuCSpan bytes) {
  auto buf = alloc(); buf->append(bytes); stream.push(ZuMv(buf));
}

static void splits() {
  ZuTestScopeRT(splits);
  for (unsigned split = 0; split <= Wire.length(); ++split) {
    Stream stream; RxFramer frame;
    push(stream, {Wire.data(), split});
    Zi::RxRange out;
    int n = frame.next_(stream, alloc, out);
    ZuCheckRT(n == (split == Wire.length() ? 1 : 0));
    if (!n) {
      push(stream, {Wire.data() + split, Wire.length() - split});
      ZuCheckRT(frame.next_(stream, alloc, out) == 1);
    }
    ZuCheckRT(out && ZuCSpan{out.cspan()} == Wire && !stream);
  }
}
static void bytes() {
  ZuTestScopeRT(bytes);
  Stream stream; RxFramer frame; Zi::RxRange out;
  unsigned allocations = 0;
  auto counted = [&allocations] { ++allocations; return alloc(); };
  const ZiIOBuf *backing = nullptr;
  for (unsigned i = 0; i < Wire.length(); ++i) {
    push(stream, {Wire.data() + i, 1});
    if (!i) backing = stream.range().buf.ptr();
    ZuCheckRT(frame.next_(stream, counted, out) == (i + 1 == Wire.length() ? 1 : 0));
    // Even a frame smaller than a receive slot must return fragment slots
    // before waiting for its suffix, so a bounded provided pool can progress.
    ZuCheckRT(stream.count_() <= 1);
  }
  ZuCheckRT(out && ZuCSpan{out.cspan()} == Wire && !stream);
  ZuCheckRT(out.buf.ptr() == backing && !allocations);
}
static void coalesced() {
  ZuTestScope(coalesced);
  Stream stream; RxFramer frame;
  auto buf = alloc(); buf->append(Wire); buf->append(Wire);
  auto backing = buf;
  stream.push(ZuMv(buf));
  Zi::RxRange first, second; Fmt::FrameView view;
  ZuCheck(frame.next_(stream, alloc, first, &view) == 1);
  ZuCheck(view.wire.data() == ZuCSpan{first.cspan()}.data() &&
    view.msgType == "D" && view.beginString == "FIX.4.4");
  ZuCheck(stream.length() == Wire.length());
  ZuCheck(first.buf == backing && first.offset == 0);
  // Retained application input survives subsequent extraction and cleanup.
  ZuCheck(frame.next_(stream, alloc, second) == 1);
  ZuCheck(second.buf == backing && second.offset == Wire.length());
  stream.clean(); frame.reset_();
  ZuCheck(ZuCSpan{first.cspan()} == Wire && ZuCSpan{second.cspan()} == Wire);
  auto exact = alloc(); exact->append(Wire); auto ptr = exact.ptr();
  stream.push(ZuMv(exact));
  ZuCheck(frame.next_(stream, alloc, first) == 1 && first.buf.ptr() == ptr);
}
static void mixed() {
  ZuTestScopeRT(mixed);
  for (unsigned split = 1; split < Wire.length(); ++split) {
    Stream stream; RxFramer frame; Zi::RxRange first, second;
    push(stream, {Wire.data(), split});
    ZuCheckRT(frame.next_(stream, alloc, first) == 0);
    auto buf = alloc();
    buf->append({Wire.data() + split, Wire.length() - split}); buf->append(Wire);
    stream.push(ZuMv(buf));
    ZuCheckRT(frame.next_(stream, alloc, first) == 1);
    ZuCheckRT(frame.next_(stream, alloc, second) == 1 && !stream);
    ZuCheckRT(ZuCSpan{first.cspan()} == Wire && ZuCSpan{second.cspan()} == Wire);
  }
}
static void gathered() {
  ZuTestScope(gathered);
  auto storage = alloc(); storage->alloc(ZiIOBuf_DefltSize << 2);
  Fmt::FrameWriter writer{{storage->ptr<char>(), storage->size},
    "FIX.4.4"};
  writer.body() << "35=D\1" "58=";
  for (unsigned i = 0; i < (ZiIOBuf_DefltSize << 1); ++i) writer.body() << 'X';
  writer.body() << '\1';
  auto wire = writer.finish(); ZuCheck(bool(wire));
  if (!wire) return;
  Stream stream; RxFramer frame; Zi::RxRange out;
  unsigned	allocations = 0;
  auto counted = [&allocations] { ++allocations; return alloc(); };
  push(stream, {wire.data.data(), 1});
  ZuCheck(frame.next_(stream, counted, out) == 0);
  push(stream, {wire.data.data() + 1, unsigned(wire.data.length()) - 1});
  ZuCheck(frame.next_(stream, counted, out) == 1);
  ZuCheck(allocations == 0 && !stream && ZuCSpan{out.cspan()} == wire.data);
  ZuCheck(out.buf->size >= wire.data.length());
}
static void refusal() {
  ZuTestScope(refusal);
  Stream stream; RxFramer frame; Zi::RxRange out;
  auto none = [] { return ZmRef<Queue::Node>{}; };
  auto retained = alloc(); retained->append({Wire.data(), 1});
  stream.push(retained);
  ZuCheck(frame.next_(stream, none, out) == 0);
  push(stream, {Wire.data() + 1, Wire.length() - 1});
  ZuCheck(frame.next_(stream, none, out) == -1);
  ZuCheck(frame.error_().code == Fmt::ErrorCode::Output);
  stream.clean(); frame.reset_();
  auto buf = alloc(); buf->append(Wire); buf->append(Wire); stream.push(ZuMv(buf));
  ZuCheck(frame.next_(stream, none, out) == 1);
  ZuCheck(out && stream.length() == Wire.length());
}
static void prefix() {
  ZuTestScopeRT(prefix);
  Stream stream; Fmt::Limits limits; limits.fieldBytes = 8;
  RxFramer frame{limits}; Zi::RxRange out;
  constexpr auto Bad = "8=aaaaaaaaa"_z;
  for (unsigned i = 0; i < Bad.length(); ++i) {
    push(stream, {Bad.data() + i, 1});
    int n = frame.next_(stream, alloc, out);
    ZuCheckRT(n == (i + 1 == Bad.length() ? -1 : 0));
  }
  ZuCheckRT(frame.error_().code == Fmt::ErrorCode::Size);
  frame.reset_(); stream.clean();
  push(stream, {Wire.data(), 5});
  ZuCheckRT(frame.next_(stream, alloc, out) == 0);
  // A disconnect drops both the stream and partial envelope state.
  frame.reset_(); stream.clean(); push(stream, Wire);
  ZuCheckRT(frame.next_(stream, alloc, out) == 1);
}
static void errors() {
  ZuTestScope(errors);
  Stream stream; RxFramer frame; Zi::RxRange out;
  auto bad = alloc(); bad->append(Wire);
  ZmRef<ZiIOBuf> corrupt = bad; corrupt->data()[corrupt->length - 2] = '8';
  stream.push(ZuMv(bad));
  ZuCheck(frame.next_(stream, alloc, out) == -1 && !out);
  ZuCheck(frame.error_().code == Fmt::ErrorCode::Checksum);
  push(stream, Wire);
  ZuCheck(frame.next_(stream, alloc, out) == -1);
  stream.clean(); frame.reset_(); push(stream, Wire);
  ZuCheck(frame.next_(stream, alloc, out) == 1);
  Fmt::Limits limits; limits.frameBytes = Wire.length() - 1;
  RxFramer bounded{limits}; push(stream, Wire);
  ZuCheck(bounded.next_(stream, alloc, out) == -1);
  ZuCheck(bounded.error_().code == Fmt::ErrorCode::Size);
}
static constexpr auto BinaryWire = "8=FIX.4.4\1" "9=136\1" "35=n\1"
  "212=35\1" "213=RTRF<8=FIX.4.4\1" "9=5\1" "35=D\1" "10=999\1\0=\xff>\1"
  "95=52\1" "96=8=FIX.4.4\1" "9=5\1" "35=D\1" "10=111\1"
  "8=FIX.4.4\1" "9=5\1" "35=8\1" "10=222\1\1"
  "11=outer\1" "93=4\1" "89=\0\1=\xff\1" "10=108\1"_z;
static constexpr auto XML = "RTRF<8=FIX.4.4\1" "9=5\1" "35=D\1" "10=999\1\0=\xff>"_z;
static constexpr auto Replay = "8=FIX.4.4\1" "9=5\1" "35=D\1" "10=111\1"
  "8=FIX.4.4\1" "9=5\1" "35=8\1" "10=222\1"_z;
struct BinaryMessage {
  ZuBSpan	xml;
  ZuCSpan	replay;
  ZuCSpan	id;
  ZuBSpan	signature;
};
ZfStruct(, (BinaryMessage, FIX),
  (xml, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes),
  (replay, (FIX::ID<95>, FIX::Binary<96>, Required), String),
  (id, (FIX::ID<11>, Required), String),
  (signature, ((FIX::Sig<93, 89>), Required), Bytes));

static void binarySplits() {
  ZuTestScopeRT(binarySplits);
  for (unsigned split = 0; split <= BinaryWire.length(); ++split) {
    Stream stream; RxFramer parser;
    push(stream, {BinaryWire.data(), split});
    Zi::RxRange first, second; Fmt::FrameView view;
    int n = parser.next_(stream, alloc, first, &view);
    ZuCheckRT(n == (split == BinaryWire.length() ? 1 : 0));
    auto tail = alloc();
    tail->append({BinaryWire.data() + split, BinaryWire.length() - split});
    tail->append(Wire); stream.push(ZuMv(tail));
    if (!n) ZuCheckRT(parser.next_(stream, alloc, first, &view) == 1);
    ZuCheckRT(first && first.cspan() == BinaryWire && view.wire.data() == ZuCSpan{first.cspan()}.data());
    if (!first) continue;
    BinaryMessage value;
    ZuCheckRT(!Fmt::load(value, view.content));
    auto original = ZuCSpan{first.cspan()}.data();
    auto xml = reinterpret_cast<const uint8_t *>(original + ZuCSpan{BinaryWire}.find(XML));
    auto replay = original + ZuCSpan{BinaryWire}.find(Replay);
    auto signature = reinterpret_cast<const uint8_t *>(
      original + ZuCSpan{BinaryWire}.find("89="_z) + 3);
    ZuCheckRT(value.xml.data() == xml && value.replay.data() == replay &&
      value.signature.data() == signature);
    ZuCheckRT(value.xml == ZuBSpan{XML} && value.replay == Replay && value.signature.length() == 4);
    ZuCheckRT(parser.next_(stream, alloc, second) == 1 && second.cspan() == Wire && !stream);
    stream.clean(); parser.reset_();
    // All three borrowed members still address the retained first message
    // allocation after extracting the next frame and cleaning the input stream.
    ZuCheckRT(ZuCSpan{first.cspan()}.data() == original && value.xml.data() == xml &&
      value.replay.data() == replay && value.signature.data() == signature &&
      value.xml == ZuBSpan{XML} && value.replay == Replay);
  }
  Stream stream; RxFramer parser; Zi::RxRange out;
  for (unsigned i = 0; i < BinaryWire.length(); ++i) {
    push(stream, {BinaryWire.data() + i, 1});
    ZuCheckRT(parser.next_(stream, alloc, out) == (i + 1 == BinaryWire.length() ? 1 : 0));
  }
  ZuCheckRT(out && out.cspan() == BinaryWire && !stream);
}

int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(binarySplits);
  ZuTestCall(splits);
  ZuTestCall(bytes);
  ZuTestCall(coalesced);
  ZuTestCall(mixed);
  ZuTestCall(gathered);
  ZuTestCall(refusal);
  ZuTestCall(prefix);
  ZuTestCall(errors);
  return 0;
}
