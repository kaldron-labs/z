//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZiRx.hh>

using namespace ZuTestUtil;

namespace ZiRxTest_ {

using RxBufAlloc = Zi::IOBufAlloc<ZiRxBuf, 32, 64, ZuStringT<"ZiRxTest.Buf">>;

struct RxHarness : public ZiRx<RxHarness> {
  enum { Normal, DropRemainder, Fail };
  int mode = Normal;
  unsigned hdrCalls = 0;
  unsigned bodyCalls = 0;
  unsigned frames = 0;
  ZtArray<Zi::RxRange> retained;

  RxHarness(unsigned maxSize = 64) : ZiRx{maxSize} { }

  auto alloc() { return ZmRef<ZiRxBuf>{new RxBufAlloc{this, 0}}; }

  int hdr(Stream &stream) {
    ++hdrCalls;
    // This protocol has exactly two header bytes; fragmented-header scratch
    // does not require assembling or rescanning the payload.
    uint8_t header[2];
    if (stream.copy(0, header) != sizeof(header)) return INT_MAX;
    return unsigned(header[0]) + sizeof(header);
  }
  int body(Stream &stream, unsigned frameLen) {
    ++bodyCalls;
    if (mode == Fail) return -1;
    if (mode == DropRemainder) return 0;
    stream.eachRange(frameLen, [this](Zi::RxRange range) -> int64_t {
      unsigned length = range.length;
      retained.push(ZuMv(range));
      return length;
    });
    ++frames;
    return frameLen;
  }
  int hdrIO(const ZiIOContext &, Stream &stream) { return hdr(stream); }
  int bodyIO(const ZiIOContext &, Stream &stream, unsigned frameLen) {
    return body(stream, frameLen);
  }
  int feed(const uint8_t *data, unsigned length) {
    return recvMem<&RxHarness::hdr, &RxHarness::body>(data, length,
      [this]() { return alloc(); });
  }
  int feedSync(const uint8_t *data, unsigned length) {
    return recvMemSync<&RxHarness::hdr, &RxHarness::body>(data, length,
      [this]() { return alloc(); });
  }
};

void testMemoryFrames()
{
  ZuTestScope(testMemoryFrames);

  RxHarness rx;
  const uint8_t first[] = {3};
  const uint8_t second[] = {0xaa, 'x', 'y', 'z', 1, 0xbb, 'q'};
  ZuCheck(!rx.feed(first, sizeof(first)));
  ZuCheck(rx.hdrCalls == 1);
  ZuCheck(rx.rxStream().length() == 1);
  ZuCheck(rx.feed(second, sizeof(second)) == sizeof(second));
  ZuCheck(rx.frames == 2);
  ZuCheck(rx.retained.length() == 3);
  ZuCheck(rx.retained[0].length == 1);
  ZuCheck(rx.retained[1].length == 4);
  ZuCheck(!memcmp(rx.retained[1].span().data(), second, 4));
  ZuCheck(rx.retained[2].length == 3);
  ZuCheck(rx.retained[1].buf.ptr() == rx.retained[2].buf.ptr());
  ZuCheck(rx.retained[2].offset == 4);
  ZuCheck(!rx.rxStream());
}

void testKnownLengthIsCached()
{
  ZuTestScope(testKnownLengthIsCached);

  RxHarness rx;
  const uint8_t first[] = {3, 0xaa};
  const uint8_t second[] = {'x'};
  const uint8_t third[] = {'y', 'z'};
  ZuCheck(!rx.feed(first, sizeof(first)));
  ZuCheck(!rx.feed(second, sizeof(second)));
  ZuCheck(rx.hdrCalls == 1);
  ZuCheck(rx.feed(third, sizeof(third)) == sizeof(third));
  ZuCheck(rx.hdrCalls == 1);
  ZuCheck(rx.frames == 1);
  ZuCheck(rx.retained.length() == 3);
}

void testMemorySizeAndBodyResults()
{
  ZuTestScope(testMemorySizeAndBodyResults);

  const uint8_t oversize[] = {120, 1};
  const uint8_t frames[] = {1, 1, 'a', 1, 2, 'b'};
  RxHarness rx;
  ZuCheck(rx.feed(oversize, sizeof(oversize)) == -1);
  ZuCheck(!rx.bodyCalls);
  rx.rxReset();
  rx.mode = RxHarness::DropRemainder;
  ZuCheck(rx.feed(frames, sizeof(frames)) == sizeof(frames));
  ZuCheck(rx.bodyCalls == 1);
  ZuCheck(!rx.rxStream());
  rx.mode = RxHarness::Fail;
  ZuCheck(rx.feed(frames, sizeof(frames)) == -1);
  ZuCheck(rx.bodyCalls == 2);
}

void testSyncMemoryKeepsTrailingRanges()
{
  ZuTestScope(testSyncMemoryKeepsTrailingRanges);

  const uint8_t first[] = {2, 1, 'a', 'b', 2, 2};
  const uint8_t second[] = {'c', 'd'};
  RxHarness rx;
  ZuCheck(!rx.feedSync(first, sizeof(first)));
  auto retained = rx.retained[0];
  ZuCheck(retained.length == 4);
  ZuCheck(retained.buf->skip == 0);
  ZuCheck(retained.buf->length == sizeof(first));
  ZuCheck(rx.rxStream().length() == 2);
  ZuCheck(rx.feedSync(second, sizeof(second)) == sizeof(second));
  ZuCheck(rx.frames == 2);
  ZuCheck(!memcmp(retained.span().data(), first, 4));
  ZuCheck(retained.buf->length == sizeof(first));
  ZuCheck(!rx.rxStream());
}

void testNetworkOriginalBacking()
{
  ZuTestScope(testNetworkOriginalBacking);

  RxHarness rx;
  ZiIOContext io;
  rx.recv<&RxHarness::hdrIO, &RxHarness::bodyIO>(io);
  auto input = rx.alloc();
  const uint8_t frames[] = {1, 1, 'a', 1, 2, 'b'};
  input->append(frames, sizeof(frames));
  auto original = input.ptr();
  io.buf = ZuMv(input);
  io.length = sizeof(frames);
  ZuCheck(io());
  ZuCheck(!io.buf);
  ZuCheck(io.initialized());
  ZuCheck(rx.frames == 2);
  ZuCheck(rx.retained[0].buf.ptr() == original);
  ZuCheck(rx.retained[1].buf.ptr() == original);
  ZuCheck(rx.retained[0].offset == 0);
  ZuCheck(rx.retained[1].offset == 3);
  ZuCheck(original->length == sizeof(frames));
  ZuCheck(original->skip == 0);
  rx.rxReset();
  ZuCheck(rx.retained[0].span()[2] == 'a');
  ZuCheck(rx.retained[1].span()[2] == 'b');
}

void testSyncNetworkFragments()
{
  ZuTestScope(testSyncNetworkFragments);

  RxHarness rx;
  ZiIOContext io;
  rx.recvSync<&RxHarness::hdrIO, &RxHarness::bodyIO>(io);
  auto first = rx.alloc();
  const uint8_t head[] = {2, 1};
  first->append(head, sizeof(head));
  auto original = first.ptr();
  io.buf = ZuMv(first);
  io.length = sizeof(head);
  ZuCheck(io());
  ZuCheck(!rx.frames);
  auto second = rx.alloc();
  const uint8_t body[] = {'a', 'b'};
  second->append(body, sizeof(body));
  io.buf = ZuMv(second);
  io.length = sizeof(body);
  ZuCheck(io());
  ZuCheck(rx.hdrCalls == 1);
  ZuCheck(rx.frames == 1);
  ZuCheck(rx.retained.length() == 2);
  ZuCheck(rx.retained[0].buf.ptr() == original);
  ZuCheck(rx.retained[1].span()[0] == 'a');
  ZuCheck(!rx.rxStream());
}

// The test protocol permits 255 payload bytes; a 64-byte slot cannot hold
// the 200-byte frame, and two slots cannot hold it by retaining fragments.
struct JumboBuf : public ZiRxBuf {
  using ZiRxBuf::ZiRxBuf;
  unsigned cookie = 0x1234;
};
using JumboAlloc = Zi::IOBufAlloc_<JumboBuf, 64, 512, ZuStringT<"ZiRxTest.Jumbo">>;

void testSharedOversize()
{
  ZuTestScope(testSharedOversize);
  RxHarness rx{512};
  unsigned allocations = 0;
  auto alloc = [&]() {
    ++allocations;
    return ZmRef<ZiRxBuf>{new JumboAlloc{&rx, 0}};
  };
  auto feed = [&](ZuBSpan bytes) {
    return rx.recvMem<&RxHarness::hdr, &RxHarness::body>(
      bytes.data(), bytes.length(), alloc);
  };
  const uint8_t prefix[] = {1, 1, 'a', 198, 0xaa};
  ZuCheck(!feed(prefix));
  ZuCheck(allocations == 2); // shared first backing requires final storage
  auto first = rx.retained[0];
  ZuCheck(first.buf->length == sizeof(prefix));
  ZuCheck(!first.buf->jumbo());
  uint8_t chunk[64];
  memset(chunk, 'x', sizeof(chunk));
  for (unsigned i = 0; i < 3; ++i) ZuCheck(!feed(chunk));
  const uint8_t tail[] = {'x', 'x', 'x', 'x', 'x', 'x', 1, 2, 'b'};
  ZuCheck(feed(tail) == sizeof(tail));
  ZuCheck(rx.frames == 3 && rx.retained.length() == 3);
  const auto &frame = rx.retained[1];
  ZuCheck(frame.length == 200 && frame.buf->jumbo());
  ZuCheck(static_cast<JumboBuf *>(frame.buf.ptr())->cookie == 0x1234);
  ZuCheck(frame.span()[0] == 198 && frame.span()[1] == 0xaa);
  for (unsigned i = 2; i < frame.length; ++i) ZuCheck(frame.span()[i] == 'x');
  ZuCheck(!memcmp(first.span().data(), prefix, 3));
  ZuCheck(first.buf->length == sizeof(prefix) && first.buf->skip == 0);
  ZuCheck(rx.retained[2].length == 3 && rx.retained[2].offset == 6);
  ZuCheck(rx.retained[2].buf->length == sizeof(tail));
  ZuCheck(!rx.rxStream());
}

#ifndef _WIN32
void testOversizeSlotReturn()
{
  ZuTestScope(testOversizeSlotReturn);
  if (!Zi::uring()) { ZuCheck(!Zi::uring()); return; }
  using Alloc = Zi::RxTCPBufAllocator<JumboBuf, 64, 512,
    ZuStringT<"ZiRxTest.Jumbo">>;
  ZmRef<Zi::IOBufAllocator> allocator = new Alloc{2};
  RxHarness rx{512};
  ZiIOContext io;
  rx.recv<&RxHarness::hdrIO, &RxHarness::bodyIO>(io);
  auto first = allocator->alloc(&rx);
  auto held = allocator->alloc(&rx);
  ZuCheck(first->uring() && held->uring());
  auto original = first.ptr();
  auto slot = first->data_();
  const uint8_t header[] = {198, 0xaa};
  first->append(header, sizeof(header));
  io.buf = ZuMv(first);
  io.length = sizeof(header);
  ZuCheck(io());
  ZuCheck(original->jumbo() && !original->uring());
  auto reused = allocator->alloc(&rx);
  ZuCheck(reused->uring() && reused->data_() == slot);
  reused = held = nullptr;
  unsigned remaining = 198;
  while (remaining) {
    auto input = allocator->alloc(&rx);
    ZuCheck(input->uring());
    unsigned n = remaining < 64 ? remaining : 64;
    memset(input->data(), 'x', n);
    input->length = n;
    io.length = n;
    io.buf = ZuMv(input);
    ZuCheck(io());
    remaining -= n;
  }
  ZuCheck(rx.hdrCalls == 1 && rx.frames == 1);
  ZuCheck(rx.retained.length() == 1);
  ZuCheck(rx.retained[0].buf.ptr() == original);
  ZuCheck(rx.retained[0].length == 200);
  ZuCheck(static_cast<JumboBuf *>(original)->cookie == 0x1234);
  for (unsigned i = 2; i < 200; ++i) ZuCheck(original->data()[i] == 'x');
  ZuCheck(!rx.rxStream());
  rx.retained.null();
  // Reset an incomplete oversized frame too; no slot remains held by it.
  auto input = allocator->alloc(&rx);
  input->append(header, sizeof(header));
  io.buf = ZuMv(input);
  ZuCheck(io());
  rx.rxReset();
  first = allocator->alloc(&rx);
  held = allocator->alloc(&rx);
  ZuCheck(first->uring() && held->uring());
}
#endif

} // ZiRxTest_

int main(int argc, char **argv)
{
  using namespace ZiRxTest_;
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testMemoryFrames);
  ZuTestCall(testKnownLengthIsCached);
  ZuTestCall(testMemorySizeAndBodyResults);
  ZuTestCall(testSyncMemoryKeepsTrailingRanges);
  ZuTestCall(testNetworkOriginalBacking);
  ZuTestCall(testSyncNetworkFragments);
  ZuTestCall(testSharedOversize);
#ifndef _WIN32
  ZuTestCall(testOversizeSlotReturn);
#endif
  return 0;
}
