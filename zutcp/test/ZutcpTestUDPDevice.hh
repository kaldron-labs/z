//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZutcpTestUDPDevice_HH
#define ZutcpTestUDPDevice_HH

#include "ZutcpTestMxPlugin.hh"

namespace UDPLeaseTest {

struct Pool : ZmNHeap<Pool>, ZutcpTest::Pool {
friend inline ZuStringT<"Zutcp.UDPTest.Pool"> ZmNHeapID(Pool *);
  struct Slot {
    uint8_t *data = nullptr;
    bool caller = false, device = false;
  };
  using Guard = ZmGuard<ZmLock>;
  mutable ZmLock lock;
  ZtArray<Slot> slots;
  unsigned releases = 0;
  using ZmNHeap<Pool>::operator new;
  using ZmNHeap<Pool>::operator delete;
  Pool(Zi::IOBufType::T type, unsigned n, unsigned bytes, unsigned head, unsigned tail,
    unsigned align) : ZutcpTest::Pool{type, n, bytes, head, tail, align, true} {
    slots.length(n);
  }
  ~Pool() override {
    for (auto &slot : slots) if (slot.data) Heap::free(slot.data, size);
  }
  Slot &slot(uint8_t *p) {
    for (auto &s : slots)
      if (s.data && uintptr_t(p) - uintptr_t(s.data) < size) return s;
    ZmFail("unknown UDP test backing");
  }
  uint8_t *alloc() override {
    Guard guard(lock);
    for (auto &s : slots) {
      if (s.caller || s.device) continue;
      if (!s.data) s.data = static_cast<uint8_t *>(Heap::alloc(size));
      s.caller = true;
      return s.data;
    }
    return nullptr;
  }
  void free(uint8_t *p) override {
    Guard guard(lock);
    auto &s = slot(p);
    ZmAssert_(s.data == p && s.caller);
    s.caller = false;
    ++releases;
  }
  bool balanced() const {
    Guard guard(lock);
    for (auto &s : slots) if (s.caller || s.device) return false;
    return true;
  }
};

struct Device : ZmNHeap<Device>, ZutcpTest::Xdp {
friend inline ZuStringT<"Zutcp.UDPTest.Device"> ZmNHeapID(Device *);
  ZmRef<Pool> pool;
  ZtArray<ZtArray<uint8_t>> frames;
  unsigned calls = 0, completions = 0;
  bool refuse = false, early = false, retire = false;
  ZmAtomic<unsigned> down{0};

  using ZmNHeap<Device>::operator new;
  using ZmNHeap<Device>::operator delete;
  using Xdp::Xdp;
  ZmRef<Zi::IOBufPool> bufPool(Zi::IOBufType::T type, unsigned count, unsigned size,
    unsigned head, unsigned tail, unsigned align) override
  {
    if (type != Zi::IOBufType::TxUDP)
      return Xdp::bufPool(type, count, size, head, tail, align);
    pool = new Pool{type, count, size, head, tail, align};
    return pool;
  }
  bool ready() const override { return !down && Xdp::ready(); }
  Zu::IO::Result txReady(uint8_t *p) const override {
    if (!ready()) return Zi::IOError;
    Pool::Guard guard(pool->lock);
    auto &s = pool->slot(p);
    ZmAssert_(s.caller);
    return s.device ? Zi::NotReady : Zi::OK;
  }
  Zu::IO::Result send(uint8_t *p, unsigned n) override {
    ++calls;
    if (!ready()) return Zi::IOError;
    Pool::Guard guard(pool->lock);
    auto &s = pool->slot(p);
    ZmAssert_(s.caller);
    if (refuse || s.device) return Zi::NotReady;
    (new (frames.push()) ZtArray<uint8_t>)->append(p, n);
    if (early) ++completions;
    else s.device = true;
    return Zi::OK;
  }
  unsigned pollTx() override {
    Pool::Guard guard(pool->lock);
    if (retire) for (auto &s : pool->slots)
      if (s.device) { s.device = false; ++completions; }
    unsigned n = completions;
    completions = 0;
    return n;
  }
};

} // UDPLeaseTest

#endif /* ZutcpTestUDPDevice_HH */
