//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRx.hh>

using namespace ZuTestUtil;

namespace ZiRxTest_ {

struct RxBuf : public ZiIOBuf {
  static inline unsigned alive = 0;
  RxBuf(uint8_t *data, uint32_t size, Zi::IOBufAllocator_ *allocator,
    void *owner = nullptr, uint32_t length = 0, bool builtin = false,
    bool registered = false) :
    ZiIOBuf{data, size, allocator, owner, length, builtin, registered} { ++alive; }
  ~RxBuf() { --alive; }
};

struct RxHarness : public ZiRx<RxHarness, 64> {
  enum BodyMode {
    Normal,
    DropRemainder,
    Fail
  };

  ZiMultiplex mx;
  Zi::IOBufAllocator *allocator = mx.allocator<
    Zi::IOBufType::App, RxBuf, 32, ZiIOBuf_DefltAlign, ZuStringT<"ZiRxTest.Buf">, 64>();
  BodyMode	mode = Normal;

  unsigned	hdrCalls = 0;
  unsigned	hdrInsufficient = 0;
  unsigned	bodyCalls = 0;
  unsigned	frames = 0;

  unsigned	syncBodyCalls = 0;
  unsigned	syncFrames = 0;

  unsigned	payloadLen[8] = {};
  char		payloadFirst[8] = {};

  auto rxAllocator() { return allocator; }

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
    const uint8_t oversize[] = { 63, 0x01 };
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
    const uint8_t oversize[] = { 63, 0x01 };
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

// Synthetic transport: all deliveries and rearming execute on the real mx Rx
// dispatcher, through the connection's actual context and owner-directed API.
struct RxPlugin : ZiMxPlugin {
  unsigned rearms = 0;
  explicit RxPlugin(ZiMultiplex *mx) : ZiMxPlugin(mx) { }
  Zi::IOBufPlugin *allocator(Zi::IOBufType::T, unsigned, unsigned, unsigned) override {
    return nullptr;
  }
  void start() override { mx()->pluginStarted({}); }
  void stop() override { mx()->pluginStopped({}); }
  void rx() override { }
  void listen(ZiListenFn, ZiFailFn, ZiConnectFn, ZiIP, uint16_t, unsigned,
    Zi::IOBufAllocator *, ZiCxnOptions) override { }
  void stopListening(ZiIP, uint16_t) override { }
  void connect(ZiConnectFn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP, uint16_t,
    Zi::IOBufAllocator *, ZiCxnOptions) override { }
  void udp(ZiConnectFn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP, uint16_t,
    Zi::IOBufAllocator *, ZiCxnOptions) override { }
  void recv(ZiConnection *) override { ++rearms; }
  void send(ZiConnection *) override { }
  void disconnect(ZiConnection *, bool) override { }
  void close(ZiConnection *, bool) override { }
  void wake() override { }
  TelKey telKey() const override { return {}; }
  void telemetry(Ztc::MxTelemetry &) const override { }
  unsigned allCxns(AllCxnsFn) const override { return 0; }
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override { return 0; }
  void watch(AddCxnFn, DelCxnFn) override { }
  void unwatch() override { }
};

struct FrameAllocator : Zi::IOBufBuiltinAllocator<
  Zi::IOBufType::App, RxBuf, 16, ZiIOBuf_DefltAlign,
  ZuStringT<"ZiRxTest.Frame">, 128> {
  unsigned allocations = 0;
  ZmRef<ZiIOBuf> allocBuf(void *owner, uint32_t len = 0, uint32_t mps = ~uint32_t(0)) override {
    ++allocations;
    return IOBufBuiltinAllocator::allocBuf(owner, len, mps);
  }
};

struct NetworkAllocator : Zi::IOBufBuiltinAllocator<
  Zi::IOBufType::RxTCP, RxBuf, 16, ZiIOBuf_DefltAlign,
  ZuStringT<"ZiRxTest.Packet">, 128> {
  bool stubs, reject = false, valid = true;
  unsigned allocations = 0, attached = 0, retired = 0;

  NetworkAllocator(bool stubs_, bool udp) : stubs{stubs_} {
    if (udp) type = Zi::IOBufType::RxUDP;
    if (stubs) { size = 16; headRoom = 3; tailRoom = 4; }
  }
  ZmRef<ZiIOBuf> allocBuf(void *owner, uint32_t len = 0, uint32_t mps = ~uint32_t(0)) override {
    ++allocations;
    valid &= !len && mps == ~uint32_t(0);
    if (reject) return nullptr;
    if (!stubs) return IOBufBuiltinAllocator::allocBuf(owner, len, mps);
    ZmRef<ZiIOBuf> buf = new Zi::IOBufStub<RxBuf, ZuStringT<"ZiRxTest.Packet">>{
      nullptr, 0, this, owner, 0, false, true};
    buf->skip = headRoom;
    return buf;
  }
  void attach(ZiIOBuf *buf) {
    if (!stubs) return;
    valid &= !buf->data_() && !buf->size && buf->skip == headRoom;
    buf->data__ = uintptr_t(malloc(size)) | ZiIOBuf::Registered;
    buf->size = size;
    ++attached;
  }
  void free(uint8_t *ptr, unsigned n, bool registered) const override {
    if (registered) {
      ++const_cast<NetworkAllocator *>(this)->retired;
      ::free(ptr);
    } else {
      IOBufBuiltinAllocator::free(ptr, n, false);
    }
  }
};

struct CxnHarness : ZiConnection, ZiRx<CxnHarness, 64> {
  using Rx = ZiRx<CxnHarness, 64>;
  enum { Normal, Drop, Fail, Complete, Disconnect, Rearm, Partial };

  FrameAllocator *frameAllocator;
  NetworkAllocator *network;
  int mode = Normal;
  bool sync = false, valid = true;
  unsigned bodies = 0, headers = 0, bytes = 0;
  unsigned prefixes[64] = {}, sizes[64] = {};
  ZiIOBuf *original = nullptr;
  ZmRef<ZiIOBuf> retained;
  ZiSockAddr source{ZiIP{"127.0.0.1"}, 4321};

  CxnHarness(ZiMultiplex *mx, FrameAllocator *app, NetworkAllocator *net, bool udp) :
    ZiConnection(mx, ZiCxnInfo{.type = udp ? ZiCxnType::UDP : ZiCxnType::TCPIn,
      .socket = Zi::nullSocket(), .mps = 1, .rxAllocator = net}),
    frameAllocator{app}, network{net} { }
  void connected(ZiRxContext &) override { }
  void disconnected(bool) override { }
  auto rxAllocator() { return frameAllocator; }
  void arm(bool synchronous = false) {
    sync = synchronous;
    if (sync) Rx::recvSync<&CxnHarness::hdr, &CxnHarness::bodySync>(rxContext());
    else Rx::recv<&CxnHarness::hdr, &CxnHarness::body>(rxContext());
  }
  bool slot() {
    auto &io = rxContext();
    return io.buf && io.buf->allocator() == network && !io.buf->length &&
      io.buf->owner == this && (!network->stubs ||
	(!io.buf->data_() && !io.buf->size && io.buf->skip == network->headRoom));
  }
  void feed(const uint8_t *data, unsigned n) {
    auto &io = rxContext();
    valid &= slot();
    if (!slot()) return;
    network->attach(io.buf);
    valid &= n <= io.buf->avail() - network->tailRoom;
    if (n > io.buf->avail() - network->tailRoom) return;
    memcpy(io.buf->data(), data, n);
    io.buf->length = n;
    io.addr = source;
    io.tos = uint8_t(2);
    original = io.buf;
    valid &= io();
    valid &= io.completed() || slot();
  }
  int hdr(const ZiRxContext &, ZiIOBuf *buf) {
    ++headers;
    valid &= buf && buf->length && buf->data_();
    if (buf->length < 2) return INT_MAX;
    if (buf->data()[1] == 255) return -1;
    if (buf->data()[1] == 254) return INT_MAX;
    return buf->data()[0] + 2;
  }
  int done(const ZiRxContext &io_, ZiIOBuf *buf, unsigned framed) {
    auto &io = const_cast<ZiRxContext &>(io_);
    valid &= slot() && io.addr == source && io.tos.is<uint8_t>() &&
      io.tos.p<uint8_t>() == 2;
    prefixes[bodies] = buf->data()[0];
    sizes[bodies] = framed;
    ++bodies;
    bytes += framed;
    switch (mode) {
      case Drop: return 0;
      case Fail: return -1;
      case Complete: io.complete(); return framed;
      case Disconnect: io.disconnect(); return framed;
      case Rearm: mode = Normal; arm(sync); return framed;
      case Partial: mode = Normal; return 2;
      default: return framed;
    }
  }
  int body(const ZiRxContext &io, ZmRef<ZiIOBuf> buf) {
    retained = buf;
    return done(io, buf, buf->length);
  }
  int bodySync(const ZiRxContext &io, ZiIOBuf *buf, unsigned framed) {
    retained = buf;
    return done(io, buf, framed);
  }
};

void testConnectionFrames(bool stubs, bool synchronous, bool udp)
{
  ZuTestScopeRT(testConnectionFrames);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(2); })
    .rxThread(1).txThread(2)};
  ZmRef<RxPlugin> plugin = new RxPlugin{&mx};
  mx.plugin(plugin);
  FrameAllocator app;
  NetworkAllocator net{stubs, udp};
  CxnHarness cxn{&mx, &app, &net, udp};
  bool started = mx.start();
  ZuCheckRT(started);
  if (!started) return;
  ZmRef<ZiIOBuf> held;
  ZmBlock<>{}([&mx, &cxn, &app, &net, &held, plugin, synchronous](auto wake) {
    mx.rxRun([&cxn, &app, &net, &held, plugin, synchronous, wake = ZuMv(wake)]() mutable {
      cxn.arm(synchronous);
      cxn.valid &= cxn.slot() && !cxn.headers;
      cxn.rxContext()(); // empty builtin slot or backingless stub
      cxn.valid &= !cxn.headers;
      const uint8_t exact[] = {1, 0, 'a'};
      unsigned before = app.allocations;
      cxn.feed(exact, sizeof(exact));
      cxn.valid &= cxn.retained.ptr() == cxn.original && app.allocations == before;
      cxn.retained = nullptr;

      // Every header/body split, including an unknown one-byte prefix.
      const uint8_t frame[] = {5, 0, 'a', 'b', 'c', 'd', 'e'};
      for (unsigned split = 1; split < sizeof(frame); ++split) {
	cxn.feed(frame, split);
	cxn.valid &= cxn.retained == nullptr && cxn.slot();
	cxn.feed(frame + split, sizeof(frame) - split);
	cxn.valid &= cxn.retained && cxn.retained->allocator() == &app &&
	  cxn.retained->length == (synchronous ? 0 : sizeof(frame)) &&
	  !memcmp(cxn.retained->data() - (synchronous ? sizeof(frame) : 0), frame,
	    sizeof(frame));
	cxn.retained = nullptr;
      }
      // Several complete frames plus a partial tail in one packet.
      const uint8_t coalesced[] = {1, 0, 'x', 1, 0, 'y', 3};
      unsigned n = cxn.bodies;
      cxn.feed(coalesced, sizeof(coalesced));
      cxn.valid &= cxn.bodies == n + 2;
      const uint8_t tail[] = {0, 'a', 'b', 'c'};
      cxn.feed(tail, sizeof(tail));
      cxn.valid &= cxn.bodies == n + 3;
      cxn.retained = nullptr;

      // Message larger than one registered packet; preserve App accumulation.
      uint8_t large[42] = {40};
      for (unsigned i = 2; i < sizeof(large); ++i) large[i] = uint8_t(i);
      n = cxn.bodies;
      for (unsigned off = 0; off < sizeof(large);) {
	unsigned len = sizeof(large) - off;
	if (len > 7) len = 7;
	cxn.feed(large + off, len);
	off += len;
      }
      cxn.valid &= cxn.bodies == n + 1 && cxn.sizes[n] == sizeof(large);
      cxn.retained = nullptr;

      if (synchronous) {
	const uint8_t partial[] = {4, 0, 2, 0, 'p', 'q'};
	n = cxn.bodies;
	cxn.mode = CxnHarness::Partial;
	cxn.feed(partial, sizeof(partial));
	cxn.valid &= cxn.bodies == n + 2 && cxn.sizes[n] == 6 &&
	  cxn.sizes[n + 1] == 4;
	cxn.retained = nullptr;
      }

      // Body drop/error/completion/replacement never restore an App target or
      // overwrite explicit callback teardown, and rearm notifies the transport.
      for (auto mode : {CxnHarness::Drop, CxnHarness::Fail, CxnHarness::Complete,
	  CxnHarness::Disconnect, CxnHarness::Rearm}) {
	cxn.mode = mode;
	n = cxn.bodies;
	unsigned rearms = plugin->rearms;
	cxn.feed(coalesced, sizeof(coalesced));
	cxn.valid &= cxn.bodies == n + 1;
	if (mode == CxnHarness::Fail || mode == CxnHarness::Disconnect)
	  cxn.valid &= cxn.rxContext().disconnected();
	if (mode == CxnHarness::Complete) cxn.valid &= cxn.rxContext().completed();
	if (mode == CxnHarness::Rearm)
	  cxn.valid &= plugin->rearms == rearms + 1 && cxn.slot();
	cxn.retained = nullptr;
	cxn.mode = CxnHarness::Normal;
	cxn.arm(synchronous);
      }
      // Bounded/invalid header paths and initial/refill allocation rejection.
      const uint8_t over[] = {63, 0};
      cxn.feed(over, sizeof(over));
      cxn.valid &= cxn.rxContext().disconnected();
      cxn.arm(synchronous);
      // Unknown-length headers may accumulate only a bounded prefix.
      uint8_t unknown[7] = {0, 254};
      cxn.feed(unknown, sizeof(unknown));
      for (unsigned i = 0; i < 9 && !cxn.rxContext().completed(); ++i)
	cxn.feed(unknown, sizeof(unknown));
      cxn.valid &= cxn.rxContext().disconnected();
      cxn.arm(synchronous);
      const uint8_t bad[] = {1, 255};
      cxn.feed(bad, sizeof(bad));
      cxn.valid &= cxn.rxContext().disconnected();
      net.reject = true;
      cxn.arm(synchronous);
      cxn.valid &= cxn.rxContext().disconnected();
      net.reject = false;
      cxn.arm(synchronous);
      net.reject = true;
      cxn.feed(exact, sizeof(exact));
      cxn.valid &= cxn.rxContext().disconnected();
      net.reject = false;
      cxn.retained = nullptr;
      cxn.rxContext().complete();
      // Completing an armed partial message releases its callback-local App
      // accumulator. A complete retained packet can instead retire off-owner.
      cxn.arm(synchronous);
      const uint8_t prefix[] = {5};
      cxn.feed(prefix, sizeof(prefix));
      cxn.rxContext().complete();
      cxn.valid &= !RxBuf::alive;
      cxn.arm(synchronous);
      cxn.feed(exact, sizeof(exact));
      held = ZuMv(cxn.retained);
      cxn.rxContext().complete();
      wake();
    });
  });
  ZuCheckRT(cxn.valid && net.valid);
  ZuCheckRT(held && held->allocator() == &net);
  held = nullptr; // final registered packet release on the control thread
  ZuCheckRT(net.attached == net.retired && !RxBuf::alive);
  ZuCheckRT(mx.stop());
}

} // ZiRxTest_

using namespace ZiRxTest_;

int main(int argc, char **argv)
{
  ZiLog::init("ZiRxTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRecvMemFragmentedAndIntMax);
  ZuTestCall(testRecvMemOversizeAndBodyModes);
  ZuTestCall(testRecvMemSyncTrailingAndErrors);
  ZuTestCall(testConnectionFrames, false, false, false);
  ZuTestCall(testConnectionFrames, false, false, true);
  ZuTestCall(testConnectionFrames, false, true, false);
  ZuTestCall(testConnectionFrames, false, true, true);
  ZuTestCall(testConnectionFrames, true, false, false);
  ZuTestCall(testConnectionFrames, true, false, true);
  ZuTestCall(testConnectionFrames, true, true, false);
  ZuTestCall(testConnectionFrames, true, true, true);
  ZiLog::stop();
  return 0;
}
