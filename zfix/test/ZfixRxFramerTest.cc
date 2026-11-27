//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZfixTestAllocator.hh"

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZfixRxFramer.hh>

using namespace ZuTestUtil;
using namespace Zfix;

constexpr char Wire[] = "8=FIX.4.4\1" "9=13\1" "35=D\1" "58=ABCD\1" "10=155\1";
using Queue = ZiRxQueue;
using Stream = ZiRxStream<Queue>;

using BufferAllocator = Zi::IOBufBuiltinAllocator<Zi::IOBufType::App, Queue::Node,
  ZiIOBuf_DefltSize>;
static auto alloc() { return ZfixTest::Allocator<BufferAllocator>::get()->allocBuf(); }
static void push(Stream &stream, ZuCSpan bytes) {
  auto buf = alloc(); buf->append(bytes); stream.push(ZuMv(buf));
}

static void splits() {
  ZuTestScope(splits);
  for (unsigned split = 0; split <= (sizeof(Wire) - 1); ++split) {
    Stream stream; RxFramer frame;
    push(stream, {Wire, split});
    ZmRef<ZiIOBuf> out;
    int n = frame.next_(stream, alloc, out);
    ZuCheck(n == (split == (sizeof(Wire) - 1) ? 1 : 0));
    if (!n) {
      push(stream, {Wire + split, (sizeof(Wire) - 1) - split});
      ZuCheck(frame.next_(stream, alloc, out) == 1);
    }
    ZuCheck(out && ZuCSpan{out->cspan()} == Wire && !stream);
  }
}
static void bytes() {
  ZuTestScope(bytes);
  Stream stream; RxFramer frame; ZmRef<ZiIOBuf> out;
  for (unsigned i = 0; i < (sizeof(Wire) - 1); ++i) {
    push(stream, {Wire + i, 1});
    ZuCheck(frame.next_(stream, alloc, out) == (i + 1 == (sizeof(Wire) - 1) ? 1 : 0));
  }
  ZuCheck(out && ZuCSpan{out->cspan()} == Wire && !stream);
}
static void coalesced() {
  ZuTestScope(coalesced);
  Stream stream; RxFramer frame;
  auto buf = alloc(); buf->append(Wire); buf->append(Wire);
  stream.push(ZuMv(buf));
  ZmRef<ZiIOBuf> first, second; Fmt::FrameView view;
  ZuCheck(frame.next_(stream, alloc, first, &view) == 1);
  ZuCheck(view.wire.data() == ZuCSpan{first->cspan()}.data() &&
    view.msgType == "D" && view.beginString == "FIX.4.4");
  ZuCheck(stream.length() == (sizeof(Wire) - 1));
  // Retained application input survives subsequent extraction and cleanup.
  ZuCheck(frame.next_(stream, alloc, second) == 1);
  stream.clean(); frame.reset_();
  ZuCheck(ZuCSpan{first->cspan()} == Wire && ZuCSpan{second->cspan()} == Wire);
  auto exact = alloc(); exact->append(Wire); auto ptr = exact.ptr();
  stream.push(ZuMv(exact));
  ZuCheck(frame.next_(stream, alloc, first) == 1 && first.ptr() == ptr);
}
static void coalescedShared() {
  ZuTestScope(coalescedShared);
  Stream stream; RxFramer frame;
  auto buf = alloc(); buf->append(Wire); buf->append(Wire); buf->append(Wire);
  auto data = buf->data();
  stream.push(ZuMv(buf));
  unsigned slices = 0, copies = 0;
  auto copied = [&copies]() -> ZmRef<Queue::Node> { ++copies; return alloc(); };
  auto slice = [&slices](Queue::Node *source, uint8_t *p, unsigned n)
      -> ZmRef<Queue::Node> {
    ++slices;
    auto heap = source->allocator();
    if (auto view = dynamic_cast<Zi::RxSlice<Queue::Node> *>(source))
      heap = view->slice.heap;
    return new Zi::RxSlice<Queue::Node>{source, p, n, heap};
  };
  ZmRef<ZiIOBuf> first, second, third; Fmt::FrameView view;
  ZuCheck(frame.nextShared_(stream, copied, slice, first, &view) == 1);
  ZuCheck(first && first->data() == data &&
    view.wire.data() == reinterpret_cast<const char *>(data) &&
    view.msgType == "D" && slices == 2 && !copies);
  ZuCheck(frame.nextShared_(stream, copied, slice, second) == 1 &&
    second->data() == data + (sizeof(Wire) - 1) && slices == 4 && !copies);
  ZuCheck(frame.nextShared_(stream, copied, slice, third) == 1 &&
    third->data() == data + ((sizeof(Wire) - 1)<<1) && !stream && !copies);
  stream.clean(); frame.reset_();
  ZuCheck(first->cspan() == Wire && second->cspan() == Wire && third->cspan() == Wire);
}
static void mixed() {
  ZuTestScope(mixed);
  for (unsigned split = 1; split < (sizeof(Wire) - 1); ++split) {
    Stream stream; RxFramer frame; ZmRef<ZiIOBuf> first, second;
    push(stream, {Wire, split});
    ZuCheck(frame.next_(stream, alloc, first) == 0);
    auto buf = alloc();
    buf->append({Wire + split, (sizeof(Wire) - 1) - split}); buf->append(Wire);
    stream.push(ZuMv(buf));
    ZuCheck(frame.next_(stream, alloc, first) == 1);
    ZuCheck(frame.next_(stream, alloc, second) == 1 && !stream);
    ZuCheck(ZuCSpan{first->cspan()} == Wire && ZuCSpan{second->cspan()} == Wire);
  }
}
static void gathered() {
  ZuTestScope(gathered);
  auto storage = alloc(); storage->alloc(ZiIOBuf_DefltSize << 2);
  Fmt::FrameWriter writer{{reinterpret_cast<char *>(storage->span().data()), storage->size},
    "FIX.4.4"};
  writer.body() << "35=D\1" "58=";
  for (unsigned i = 0; i < (ZiIOBuf_DefltSize << 1); ++i) writer.body() << 'X';
  writer.body() << '\1';
  auto wire = writer.finish(); ZuCheck(bool(wire));
  if (!wire) return;
  Stream stream; RxFramer frame; ZmRef<ZiIOBuf> out;
  unsigned	allocations = 0;
  auto counted = [&allocations] { ++allocations; return alloc(); };
  push(stream, {wire.data.data(), 1});
  ZuCheck(frame.next_(stream, counted, out) == 0);
  push(stream, {wire.data.data() + 1, unsigned(wire.data.length()) - 1});
  ZuCheck(frame.next_(stream, counted, out) == 1);
  ZuCheck(allocations == 1 && !stream && ZuCSpan{out->cspan()} == wire.data);
  ZuCheck(out->size == wire.data.length());
}
static void refusal() {
  ZuTestScope(refusal);
  Stream stream; RxFramer frame; ZmRef<ZiIOBuf> out;
  auto none = [] { return ZmRef<Queue::Node>{}; };
  push(stream, {Wire, 1});
  ZuCheck(frame.next_(stream, none, out) == -1);
  ZuCheck(frame.error_().code == Fmt::ErrorCode::Output && stream.length() == 1);
  stream.clean(); frame.reset_();
  auto buf = alloc(); buf->append(Wire); buf->append(Wire); stream.push(ZuMv(buf));
  ZuCheck(frame.next_(stream, none, out) == -1);
  ZuCheck(!out && stream.length() == 2 * (sizeof(Wire) - 1));
}
static void prefix() {
  ZuTestScope(prefix);
  Stream stream; Fmt::Limits limits; limits.fieldBytes = 8;
  RxFramer frame{limits}; ZmRef<ZiIOBuf> out;
  constexpr char Bad[] = "8=aaaaaaaaa";
  for (unsigned i = 0; i < (sizeof(Bad) - 1); ++i) {
    push(stream, {Bad + i, 1});
    int n = frame.next_(stream, alloc, out);
    ZuCheck(n == (i + 1 == (sizeof(Bad) - 1) ? -1 : 0));
  }
  ZuCheck(frame.error_().code == Fmt::ErrorCode::Size);
  frame.reset_(); stream.clean();
  push(stream, {Wire, 5});
  ZuCheck(frame.next_(stream, alloc, out) == 0);
  // A disconnect drops both the stream and partial envelope state.
  frame.reset_(); stream.clean(); push(stream, Wire);
  ZuCheck(frame.next_(stream, alloc, out) == 1);
}
static void errors() {
  ZuTestScope(errors);
  Stream stream; RxFramer frame; ZmRef<ZiIOBuf> out;
  auto bad = alloc(); bad->append(Wire);
  ZmRef<ZiIOBuf> corrupt = bad; corrupt->data()[corrupt->length - 2] = '8';
  stream.push(ZuMv(bad));
  ZuCheck(frame.next_(stream, alloc, out) == -1 && !out);
  ZuCheck(frame.error_().code == Fmt::ErrorCode::Checksum);
  push(stream, Wire);
  ZuCheck(frame.next_(stream, alloc, out) == -1);
  stream.clean(); frame.reset_(); push(stream, Wire);
  ZuCheck(frame.next_(stream, alloc, out) == 1);
  Fmt::Limits limits; limits.frameBytes = (sizeof(Wire) - 1) - 1;
  RxFramer bounded{limits}; push(stream, Wire);
  ZuCheck(bounded.next_(stream, alloc, out) == -1);
  ZuCheck(bounded.error_().code == Fmt::ErrorCode::Size);
}
static constexpr char BinaryWire[] = "8=FIX.4.4\1" "9=136\1" "35=n\1"
  "212=35\1" "213=RTRF<8=FIX.4.4\1" "9=5\1" "35=D\1" "10=999\1\0=\xff>\1"
  "95=52\1" "96=8=FIX.4.4\1" "9=5\1" "35=D\1" "10=111\1"
  "8=FIX.4.4\1" "9=5\1" "35=8\1" "10=222\1\1"
  "11=outer\1" "93=4\1" "89=\0\1=\xff\1" "10=108\1";
static constexpr char XML[] = "RTRF<8=FIX.4.4\1" "9=5\1" "35=D\1" "10=999\1\0=\xff>";
static constexpr char Replay[] = "8=FIX.4.4\1" "9=5\1" "35=D\1" "10=111\1"
  "8=FIX.4.4\1" "9=5\1" "35=8\1" "10=222\1";
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
  ZuTestScope(binarySplits);
  for (unsigned split = 0; split <= (sizeof(BinaryWire) - 1); ++split) {
    Stream stream; RxFramer parser;
    push(stream, {BinaryWire, split});
    ZmRef<ZiIOBuf> first, second; Fmt::FrameView view;
    int n = parser.next_(stream, alloc, first, &view);
    ZuCheck(n == (split == (sizeof(BinaryWire) - 1) ? 1 : 0));
    auto tail = alloc();
    tail->append({BinaryWire + split, (sizeof(BinaryWire) - 1) - split});
    tail->append(Wire); stream.push(ZuMv(tail));
    if (!n) ZuCheck(parser.next_(stream, alloc, first, &view) == 1);
    ZuCheck(first && first->cspan() == BinaryWire && view.wire.data() == ZuCSpan{first->cspan()}.data());
    if (!first) continue;
    BinaryMessage value;
    ZuCheck(!Fmt::load(value, view.content));
    auto original = ZuCSpan{first->cspan()}.data();
    auto xml = reinterpret_cast<const uint8_t *>(original + ZuCSpan{BinaryWire}.find(XML));
    auto replay = original + ZuCSpan{BinaryWire}.find(Replay);
    auto signature = reinterpret_cast<const uint8_t *>(
      original + ZuCSpan{BinaryWire}.find("89=") + 3);
    ZuCheck(value.xml.data() == xml && value.replay.data() == replay &&
      value.signature.data() == signature);
    ZuCheck(value.xml == ZuBSpan{XML} && value.replay == Replay && value.signature.length() == 4);
    ZuCheck(parser.next_(stream, alloc, second) == 1 && second->cspan() == Wire && !stream);
    stream.clean(); parser.reset_();
    // All three borrowed members still address the retained first message
    // allocation after extracting the next frame and cleaning the input stream.
    ZuCheck(ZuCSpan{first->cspan()}.data() == original && value.xml.data() == xml &&
      value.replay.data() == replay && value.signature.data() == signature &&
      value.xml == ZuBSpan{XML} && value.replay == Replay);
  }
  Stream stream; RxFramer parser; ZmRef<ZiIOBuf> out;
  for (unsigned i = 0; i < (sizeof(BinaryWire) - 1); ++i) {
    push(stream, {BinaryWire + i, 1});
    ZuCheck(parser.next_(stream, alloc, out) == (i + 1 == (sizeof(BinaryWire) - 1) ? 1 : 0));
  }
  ZuCheck(out && out->cspan() == BinaryWire && !stream);
}

int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(binarySplits);
  ZuTestCall(splits);
  ZuTestCall(bytes);
  ZuTestCall(coalesced);
  ZuTestCall(coalescedShared);
  ZuTestCall(mixed);
  ZuTestCall(gathered);
  ZuTestCall(refusal);
  ZuTestCall(prefix);
  ZuTestCall(errors);
  return 0;
}
