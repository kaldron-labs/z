//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Finite packet backing for allocation-failure and layout tests.

#ifndef ZquicFiniteProvider_HH
#define ZquicFiniteProvider_HH

#include <assert.h>

#include <zlib/ZuIntrin.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>

class ZquicFiniteProvider :
  public ZmNHeap<ZquicFiniteProvider>, public Zi::IOBufPool {
friend inline ZuStringT<"Zquic.Test.FiniteProvider"> ZmNHeapID(ZquicFiniteProvider *);

public:
  enum { MaxSlots = 8, SlotSize = 1536, HeadRoom = 32, TailRoom = 32 };

  explicit ZquicFiniteProvider(ZiMultiplex *, unsigned slots = 1) :
    m_mask{(1U << slots) - 1}, m_shared{m_mask}
  {
    assert(slots && slots <= MaxSlots);
    count = slots;
    size = SlotSize;
    align = Zm::CacheLineSize;
    headRoom = HeadRoom;
    tailRoom = TailRoom;
  }
  ~ZquicFiniteProvider() {
    assert(unsigned(m_shared.free) == m_mask);
  }

  uint8_t *alloc() override {
    unsigned available = m_shared.free;
    while (available) {
      unsigned slot = ZuIntrin::ctz(available);
      unsigned next = available & ~(1U << slot);
      unsigned observed = m_shared.free.cmpXch(next, available);
      if (observed == available) return m_data[slot];
      available = observed;
    }
    return nullptr;
  }

  void free(uint8_t *data) override {
    uintptr_t offset = uintptr_t(data) - uintptr_t(m_data);
    unsigned slot = unsigned(offset / SlotSize);
    assert(!(offset % SlotSize) && slot < MaxSlots && (m_mask & (1U << slot)));
    m_shared.free |= 1U << slot;
  }

  template <typename L> void ownerLayout(L l) const {
    l(&m_shared, sizeof(m_shared));
    for (const auto &slot : m_data) l(slot, sizeof(slot));
  }

private:
  const unsigned m_mask;
  struct alignas(Zm::CacheLineSize) Shared {
    ZmAtomic<unsigned> free;
  } m_shared;
  alignas(Zm::CacheLineSize) uint8_t m_data[MaxSlots][SlotSize];
};

#endif /* ZquicFiniteProvider_HH */
