//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiRx.hh>

using namespace ZuTestUtil;

namespace {

struct RxBuf : public ZiIOBuf {
  RxBuf(uint8_t *data, unsigned size) :
    ZiIOBuf(data, size) { }
  RxBuf(uint8_t *data, unsigned size, void *owner) :
    ZiIOBuf(data, size, owner) { }
  RxBuf(uint8_t *data, unsigned size, void *owner, unsigned length) :
    ZiIOBuf(data, size, owner, length) { }
};
using RxBufAlloc = Zi::IOBufAlloc<RxBuf, 32, 64, ZuStringT<"ZiRxTest.Buf">>;

struct RxHarness : public ZiRx<RxHarness, RxBufAlloc> {
  enum BodyMode {
    Normal,
    DropRemainder,
    Fail
  };

  BodyMode	mode = Normal;

  unsigned	hdrCalls = 0;
  unsigned	hdrInsufficient = 0;
  unsigned	bodyCalls = 0;
  unsigned	frames = 0;

  unsigned	syncBodyCalls = 0;
  unsigned	syncFrames = 0;

  unsigned	payloadLen[8] = {};
  char		payloadFirst[8] = {};

  void reset(BodyMode mode_ = Normal)
  {
    mode = mode_;
    hdrCalls = 0;
    hdrInsufficient = 0;
    bodyCalls = 0;
    frames = 0;
    syncBodyCalls = 0;
    syncFrames = 0;
    ::memset(payloadLen, 0, sizeof(payloadLen));
    ::memset(payloadFirst, 0, sizeof(payloadFirst));
  }

  int hdr(const ZiIOBuf *buf)
  {
    ++hdrCalls;
    if (buf->length < 2) {
      ++hdrInsufficient;
      return INT_MAX;
    }
    return int(buf->data()[0]) + 2;
  }

  int body(ZmRef<ZiIOBuf> buf)
  {
    ++bodyCalls;

    unsigned frameLen = buf->length;
    unsigned payload = buf->data()[0];
    if (frames < 8) {
      payloadLen[frames] = payload;
      payloadFirst[frames] = payload ? char(buf->data()[2]) : 0;
    }

    if (mode == Fail) return -1;
    if (mode == DropRemainder) return 0;

    ++frames;
    return int(frameLen);
  }

  int bodySync(ZiIOBuf *buf, unsigned frameLen)
  {
    ++syncBodyCalls;

    unsigned payload = buf->data()[0];
    if (syncFrames < 8) {
      payloadLen[syncFrames] = payload;
      payloadFirst[syncFrames] = payload ? char(buf->data()[2]) : 0;
    }

    if (mode == Fail) return -1;
    if (mode == DropRemainder) return 0;

    ++syncFrames;
    return int(frameLen);
  }
};

void testRecvMemFragmentedAndIntMax()
{
  ZuTestScope(testRecvMemFragmentedAndIntMax);

  RxHarness rx;
  rx.reset();

  ZmRef<ZiIOBuf> buf;

  // Header requires at least 2 bytes; this should return INT_MAX path.
  {
    const uint8_t p1[] = { 3 };
    int rc = rx.recvMem<&RxHarness::hdr, &RxHarness::body>(p1, 1, buf);
    ZuCheck(rc == 0);
    ZuCheck(rx.hdrInsufficient == 1);
    ZuCheck(rx.bodyCalls == 0);
    ZuCheck(!!buf);
    ZuCheck(buf->length == 1);
  }

  {
    // complete frame: [payload-len=3][tag][payload...]
    const uint8_t p2[] = { 0xAA, 'x', 'y', 'z' };
    int rc = rx.recvMem<&RxHarness::hdr, &RxHarness::body>(p2, 4, buf);
    ZuCheck(rc == 4);
    ZuCheck(rx.bodyCalls == 1);
    ZuCheck(rx.frames == 1);
    ZuCheck(rx.payloadLen[0] == 3);
    ZuCheck(rx.payloadFirst[0] == 'x');
    ZuCheck(!buf);
  }
}

void testRecvMemOversizeAndBodyModes()
{
  ZuTestScope(testRecvMemOversizeAndBodyModes);

  RxHarness rx;
  ZmRef<ZiIOBuf> buf;

  // Oversize frame rejected before body processing.
  rx.reset();
  {
    const uint8_t oversize[] = { 120, 0x01 };
    int rc = rx.recvMem<&RxHarness::hdr, &RxHarness::body>(oversize, 2, buf);
    ZuCheck(rc == -1);
    ZuCheck(rx.bodyCalls == 0);
  }

  // Body can explicitly drop all trailing data by returning 0.
  rx.reset(RxHarness::DropRemainder);
  buf = nullptr;
  {
    const uint8_t twoFrames[] = {
      1, 0x01, 'a',
      1, 0x02, 'b'
    };
    int rc = rx.recvMem<&RxHarness::hdr, &RxHarness::body>(twoFrames, 6, buf);
    ZuCheck(rc == 6);
    ZuCheck(rx.bodyCalls == 1);
    ZuCheck(rx.frames == 0);
  }

  // Body error disconnect path.
  rx.reset(RxHarness::Fail);
  buf = nullptr;
  {
    const uint8_t oneFrame[] = { 1, 0x01, 'q' };
    int rc = rx.recvMem<&RxHarness::hdr, &RxHarness::body>(oneFrame, 3, buf);
    ZuCheck(rc == -1);
    ZuCheck(rx.bodyCalls == 1);
  }
}

void testRecvMemSyncTrailingAndErrors()
{
  ZuTestScope(testRecvMemSyncTrailingAndErrors);

  RxHarness rx;
  rx.reset();

  ZmRef<ZiIOBuf> buf;

  {
    // one full frame + partial next frame header (tests trailing-data retention)
    const uint8_t p1[] = { 2, 0x01, 'a', 'b', 2, 0x02 };
    int rc = rx.recvMemSync<&RxHarness::hdr, &RxHarness::bodySync>(p1, 6, buf);
    ZuCheck(rc == 0);
    ZuCheck(rx.syncBodyCalls == 1);
    ZuCheck(rx.syncFrames == 1);
    ZuCheck(!!buf);
    ZuCheck(buf->skip == 4);
    ZuCheck(buf->length == 2);
  }

  {
    const uint8_t p2[] = { 'c', 'd' };
    int rc = rx.recvMemSync<&RxHarness::hdr, &RxHarness::bodySync>(p2, 2, buf);
    ZuCheck(rc == 2);
    ZuCheck(rx.syncBodyCalls == 2);
    ZuCheck(rx.syncFrames == 2);
    ZuCheck(!!buf);
    ZuCheck(buf->skip == 0);
    ZuCheck(buf->length == 0);
  }

  rx.reset();
  buf = nullptr;
  {
    const uint8_t oversize[] = { 120, 0x01 };
    int rc = rx.recvMemSync<&RxHarness::hdr, &RxHarness::bodySync>(oversize, 2, buf);
    ZuCheck(rc == -1);
    ZuCheck(rx.syncBodyCalls == 0);
  }

  rx.reset(RxHarness::Fail);
  buf = nullptr;
  {
    const uint8_t oneFrame[] = { 1, 0x01, 'z' };
    int rc = rx.recvMemSync<&RxHarness::hdr, &RxHarness::bodySync>(oneFrame, 3, buf);
    ZuCheck(rc == -1);
    ZuCheck(rx.syncBodyCalls == 1);
  }
}

} // namespace

int main(int argc, char **argv)
{
  ZiLog::init("ZiRxTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRecvMemFragmentedAndIntMax);
  ZuTestCall(testRecvMemOversizeAndBodyModes);
  ZuTestCall(testRecvMemSyncTrailingAndErrors);
  ZiLog::stop();
  return 0;
}
