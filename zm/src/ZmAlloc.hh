//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// safe alloca() smart pointer that stack allocates if requested size
// is less than 50% of the remaining stack space, falling back to RAII heap
//
// WARNING: ZmAlloc(T, N) is a macro that evaluates N multiple times
//
// {
//   auto x = ZmAlloc(uint8_t, 1024);
//   uint8_t *ptr = &x[0];
//   uint8_t byte = *x;
//   ...
// } // x is automatically freed (if needed) as it goes out of scope

#ifndef ZmAlloc_HH
#define ZmAlloc_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmStackAvail.hh>

template <typename T>
struct ZmAlloc_ {
  T *ptr = nullptr;

  ZmAlloc_() = default;
  ZmAlloc_(T *ptr_) : ptr{ptr_} { }
  ZmAlloc_(const ZmAlloc_ &) = delete;
  ZmAlloc_ &operator =(const ZmAlloc_ &) = delete;
  ZmAlloc_(ZmAlloc_ &&a) : ptr{a.ptr} { a.ptr = nullptr; }
  ZmAlloc_ &operator =(ZmAlloc_ &&a) {
    ptr = a.ptr;
    a.ptr = nullptr;
    return *this;
  }
  ~ZmAlloc_() {
    if (ZuUnlikely(!ptr)) return;
    uint8_t *ptr_ = reinterpret_cast<uint8_t *>(ptr);
    auto self = ZmSelf();
    auto addr = reinterpret_cast<uint8_t *>(self->stackAddr());
    auto size = self->stackSize();
    if (ZuLikely(ptr_ >= addr && ptr_ < (addr + size)))
      ++self->m_allocStack;
    else {
      ++self->m_allocHeap;
      Zm::alignedFree(ptr_);
    }
  }

  ZuInline bool operator !() const { return !ptr; }
  ZuInline T &operator [](unsigned i) { return ptr[i]; }
  ZuInline const T &operator [](unsigned i) const { return ptr[i]; }
};

#define ZmAlloc(T, n) \
  ZmAlloc_<T>{static_cast<T *>(!(n) ? nullptr : \
    (((ZmStackAvail()>>1) < ((n) * sizeof(T) + alignof(T))) ? \
      Zm::alignedAlloc<alignof(T)>((n) * sizeof(T)) : \
	ZuAlloca((n) * sizeof(T), alignof(T))))}

#endif /* ZmLocal_HH */
