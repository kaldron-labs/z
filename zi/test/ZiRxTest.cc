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

  RxHarness() : ZiRx{64} { }

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
  return 0;
}
