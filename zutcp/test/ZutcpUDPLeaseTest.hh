//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZutcpUDPLeaseTest_HH
#define ZutcpUDPLeaseTest_HH

#include "ZutcpTestInstance.hh"

namespace UDPLeaseTest {

// Reuse requires both leases to retire, independently of callback/CQ order.
struct Pool : ZmNHeap<Pool>, ZutcpTest::Pool {
friend inline ZuStringT<"Zutcp.UDPTest.Pool"> ZmNHeapID(Pool *);
  uint8_t *backing = nullptr;
  unsigned releases = 0;
  bool caller = false, kernel = false;

  using ZmNHeap<Pool>::operator new;
  using ZmNHeap<Pool>::operator delete;
  using ZutcpTest::Pool::Pool;
  ~Pool() override { if (backing) Heap::free(backing, size); }
  uint8_t *alloc() override {
    if (!available || caller || kernel) return nullptr;
    if (!backing) backing = static_cast<uint8_t *>(Heap::alloc(size));
    caller = true;
    return backing;
  }
  void free(uint8_t *p) override {
    ZmAssert_(p == backing && caller);
    caller = false;
    ++releases;
  }
};

struct Device : ZmNHeap<Device>, ZutcpTest::Xdp {
friend inline ZuStringT<"Zutcp.UDPTest.Device"> ZmNHeapID(Device *);
  ZmRef<Pool> pool;
  uint8_t *wire = nullptr;
  unsigned calls = 0, length = 0, completions = 0;
  bool refuse = false, early = false, retire = false;

  using ZmNHeap<Device>::operator new;
  using ZmNHeap<Device>::operator delete;
  using Xdp::Xdp;
  ZmRef<Zi::IOBufPool> bufPool(Zi::IOBufType::T type, unsigned count, unsigned size,
    unsigned head, unsigned tail, unsigned align) override
  {
    if (type != Zi::IOBufType::TxUDP)
      return Xdp::bufPool(type, count, size, head, tail, align);
    pool = new Pool{type, count, size, head, tail, align, true};
    return pool;
  }
  bool txReady(uint8_t *) const override {
    return ready() && pool && pool->caller && !pool->kernel;
  }
  bool send(uint8_t *p, unsigned n) override {
    ++calls;
    wire = p;
    length = n;
    if (refuse || !pool->caller || pool->kernel) return false;
    pool->kernel = true;
    if (early) { pool->kernel = false; ++completions; }
    return true;
  }
  unsigned pollTx() override {
    if (retire && pool->kernel) { pool->kernel = false; ++completions; }
    unsigned n = completions;
    completions = 0;
    return n;
  }
};

class Cxn : public ZmNHeap<Cxn>, public ZiConnection {
friend inline ZuStringT<"Zutcp.UDPTest.Cxn"> ZmNHeapID(Cxn *);
public:
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  Cxn(ZiMultiplex *mx, const ZiCxnInfo &info) : ZiConnection{mx, info} { }
  void connected(ZiRxContext &) override { }
  void disconnected(bool) override { }
};

} // UDPLeaseTest

static void testUDPLeases()
{
  ZuTestScope(testUDPLeases);
  auto env = ZutcpTest::fixtureEnv();
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(3); })
    .rxThread(1).txThread(2)};
  ZmRef<UDPLeaseTest::Device> device = new UDPLeaseTest::Device{&mx};
  ZmRef<Zutcp::Instance> stack = new Zutcp::Instance;
  ZmRef<Zpkt::Nif> backend = device;
  bool configured = stack->init(&mx, env, ZuMv(backend), {}).is<void>();
  auto plugin = stack;
  mx.plugin(ZuMv(plugin));
  auto tx = mx.allocator<Zi::IOBufType::TxUDP, ZiIOBuf, 128>(1);
  auto app = mx.allocator<Zi::IOBufType::App, ZiIOBuf, 128>();
  bool started = configured && tx && app && mx.start();
  ZuCHECK(started, "start the real UDP adapter with a deterministic registered slot");
  if (!started) return;
  bool passed = ZmBlock<bool>{}([&mx, &stack, &device, tx, app](auto done) {
    mx.txRun([&mx, &stack, &device, tx, app, done = ZuMv(done)]() mutable {
      bool ok = true;
      for (bool ipv6 : {false, true}) {
        auto route = path(ipv6);
        ZiCxnInfo info{.type = ZiCxnType::UDP, .socket = Zi::nullSocket(),
          .options = ZiCxnOptions{}.udp(true),
          .local = {route.local, 2381}, .remote = {route.remote, 49252}};
        ZmRef<UDPLeaseTest::Cxn> cxn = new UDPLeaseTest::Cxn{&mx, info};
        for (unsigned mode = 0; mode < 5; ++mode) {
          device->refuse = mode == 0;
          device->early = mode == 1;
          device->retire = false;
          bool unsupported = mode >= 3;
          auto buf = (mode == 3 ? app : tx)->allocBuf(cxn.ptr());
          if (!buf || !buf->append("abcde")) { ok = false; break; }
          auto original = buf.ptr();
          auto backing = buf->data_();
          auto skip = buf->skip;
          unsigned calls = device->calls, releases = device->pool->releases;
          auto &io = cxn->txContext();
          bool callback = false;
          io.init(ZiTxFn{[&ok, &callback, &device, tx, original, backing, skip,
              unsupported, mode, size = buf->size, owner = buf->owner](ZiTxContext &io) {
            callback = true;
            bool refused = mode == 0 || unsupported;
            ok &= io.length == (refused ? -1 : 5) && io.buf.ptr() == original &&
              io.buf->data_() == backing && io.buf->skip == skip &&
              io.buf->length == 5 && io.buf->size == size && io.buf->owner == owner &&
              io.buf->cspan() == "abcde" &&
              io.offset == unsigned(mode == 4);
            if (mode != 3) {
              ok &= device->pool->caller && !tx->allocBuf();
              ok &= device->pool->kernel == (mode == 2);
            }
            io.complete();
            return true;
          }}, ZuMv(buf), info.remote, ZiTOS{uint8_t(0xab)});
          io.offset = unsigned(mode == 4);
          ZutcpTest::Instance::udpSend(*stack, cxn.ptr(), route);
          ok &= callback && device->calls == calls + !unsupported;
          if (mode == 3) continue;
          ok &= !device->pool->caller && device->pool->releases == releases + 1;
          if (mode == 1 || mode == 2) {
            Zutcp::IPView ip;
            Zutcp::UDPView udp;
            ok &= Zutcp::readIP({device->wire, device->length}, ip) &&
              Zutcp::readUDP(ip, udp) && ip.tos == 0xab &&
              udp.payload.data() == backing + skip && udp.payload == "abcde";
          }
          if (mode == 2) ok &= !tx->allocBuf();
          device->retire = true;
          stack->reconcileTx();
          auto returned = tx->allocBuf();
          ok &= returned && returned->data_() == backing;
          returned = nullptr;
        }
        cxn->txUp() = 0;
        cxn->rxUp() = 0;
      }
      done(ok);
    });
  });
  bool stopped = mx.stop();
  ZuCHECK(passed && stopped,
    "UDP keeps one backing through refusal, CQ-before-callback and CQ-after-callback");
}

#endif /* ZutcpUDPLeaseTest_HH */
