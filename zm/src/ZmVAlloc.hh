//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// safe alloca() backing storage that stack allocates if requested size
// is less than 50% of the remaining stack space, falling back to ZmVHeap
//
// WARNING: ZmVAlloc(A, T, N) is a macro that evaluates N multiple times

#ifndef ZmVAlloc_HH
#define ZmVAlloc_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmVHeap.hh>
#include <zlib/ZmStackAvail.hh>

template <typename VHeap, typename T>
struct ZmVAlloc_ {
  T *ptr = nullptr;

  ZmVAlloc_() = default;
  ZmVAlloc_(T *ptr_) : ptr{ptr_} { }
  ZmVAlloc_(const ZmVAlloc_ &) = delete;
  ZmVAlloc_ &operator =(const ZmVAlloc_ &) = delete;
  ZmVAlloc_(ZmVAlloc_ &&a) : ptr{a.ptr} { a.ptr = nullptr; }
  ZmVAlloc_ &operator =(ZmVAlloc_ &&a) = delete;
  ~ZmVAlloc_() {
    if (ZuUnlikely(!ptr)) return;
    uint8_t *ptr_ = reinterpret_cast<uint8_t *>(ptr);
    auto self = ZmSelf();
    auto addr = reinterpret_cast<uint8_t *>(self->stackAddr());
    auto size = self->stackSize();
    if (ZuLikely(ptr_ >= addr && ptr_ < (addr + size)))
      ++self->m_allocStack;
    else {
      ++self->m_allocHeap;
      VHeap::vfree(ptr_);
    }
  }

  ZuInline bool operator !() const { return !ptr; }
  ZuInline T &operator [](unsigned i) { return ptr[i]; }
  ZuInline const T &operator [](unsigned i) const { return ptr[i]; }
};

// ZuDecay enables the caller to pass VHeap as `typename T::VHeap`
#define ZmVAlloc(VHeap, T, n) \
  ZmVAlloc_<VHeap, T>{static_cast<T *>(!(n) ? nullptr : \
    (((ZmStackAvail()>>1) < ((n) * sizeof(T) + alignof(T))) ? \
      ZuDecay<VHeap>::valloc((n) * sizeof(T)) : \
	ZuAlloca((n) * sizeof(T), alignof(T))))}

#endif /* ZmVAlloc_HH */
