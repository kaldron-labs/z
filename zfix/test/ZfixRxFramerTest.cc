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

constexpr auto Wire = "8=FIX.4.4\1" "9=18\1" "35=D\1" "95=4\1" "96=A\1\0=\1" "10=247\1"_z;
using Queue = ZiRxQueue;
using Stream = ZiRxStream<Queue>;

using Buffer = Zi::IOBufAlloc<Queue::Node, ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.RxFramerTest">>;
static auto alloc() { return ZmRef<Queue::Node>{new Buffer}; }
static void push(Stream &stream, ZuCSpan bytes) {
  auto buf = alloc(); buf->append(bytes); stream.push(ZuMv(buf));
}

static void splits() {
  ZuTestScope(splits);
  for (unsigned split = 0; split <= Wire.length(); ++split) {
    Stream stream; RxFramer frame;
    push(stream, {Wire.data(), split});
    ZmRef<ZiIOBuf> out;
    int n = frame.next_(stream, alloc, out);
    ZuCheck(n == (split == Wire.length() ? 1 : 0));
    if (!n) {
      push(stream, {Wire.data() + split, Wire.length() - split});
      ZuCheck(frame.next_(stream, alloc, out) == 1);
    }
    ZuCheck(out && ZuCSpan{out->cspan()} == Wire && !stream);
  }
}
static void bytes() {
  ZuTestScope(bytes);
  Stream stream; RxFramer frame; ZmRef<ZiIOBuf> out;
  for (unsigned i = 0; i < Wire.length(); ++i) {
    push(stream, {Wire.data() + i, 1});
    ZuCheck(frame.next_(stream, alloc, out) == (i + 1 == Wire.length() ? 1 : 0));
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
  ZuCheck(stream.length() == Wire.length());
  // Retained application input survives subsequent extraction and cleanup.
  ZuCheck(frame.next_(stream, alloc, second) == 1);
  stream.clean(); frame.reset_();
  ZuCheck(ZuCSpan{first->cspan()} == Wire && ZuCSpan{second->cspan()} == Wire);
  auto exact = alloc(); exact->append(Wire); auto ptr = exact.ptr();
  stream.push(ZuMv(exact));
  ZuCheck(frame.next_(stream, alloc, first) == 1 && first.ptr() == ptr);
}
static void mixed() {
  ZuTestScope(mixed);
  for (unsigned split = 1; split < Wire.length(); ++split) {
    Stream stream; RxFramer frame; ZmRef<ZiIOBuf> first, second;
    push(stream, {Wire.data(), split});
    ZuCheck(frame.next_(stream, alloc, first) == 0);
    auto buf = alloc();
    buf->append({Wire.data() + split, Wire.length() - split}); buf->append(Wire);
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
  unsigned allocations = 0;
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
  push(stream, {Wire.data(), 1});
  ZuCheck(frame.next_(stream, none, out) == -1);
  ZuCheck(frame.error_().code == Fmt::ErrorCode::Output && stream.length() == 1);
  stream.clean(); frame.reset_();
  auto buf = alloc(); buf->append(Wire); buf->append(Wire); stream.push(ZuMv(buf));
  ZuCheck(frame.next_(stream, none, out) == -1);
  ZuCheck(!out && stream.length() == 2 * Wire.length());
}
static void prefix() {
  ZuTestScope(prefix);
  Stream stream; Fmt::Limits limits; limits.fieldBytes = 8;
  RxFramer frame{limits}; ZmRef<ZiIOBuf> out;
  constexpr auto Bad = "8=aaaaaaaaa"_z;
  for (unsigned i = 0; i < Bad.length(); ++i) {
    push(stream, {Bad.data() + i, 1});
    int n = frame.next_(stream, alloc, out);
    ZuCheck(n == (i + 1 == Bad.length() ? -1 : 0));
  }
  ZuCheck(frame.error_().code == Fmt::ErrorCode::Size);
  frame.reset_(); stream.clean();
  push(stream, {Wire.data(), 5});
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
  Fmt::Limits limits; limits.frameBytes = Wire.length() - 1;
  RxFramer bounded{limits}; push(stream, Wire);
  ZuCheck(bounded.next_(stream, alloc, out) == -1);
  ZuCheck(bounded.error_().code == Fmt::ErrorCode::Size);
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
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
