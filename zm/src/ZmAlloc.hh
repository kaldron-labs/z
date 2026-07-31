//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// safe alloca() backing storage that stack allocates if requested size
// is less than 50% of the remaining stack space, falling back to RAII heap
//
// WARNING: ZmAlloc(T, N) is a macro that evaluates N multiple times
//
// {
//   auto x = ZmAlloc(uint8_t, 1024);
//   uint8_t *ptr = &x[0];
//   uint8_t byte = *x;
//   ...
// } // if x was heap-allocated, it is freed as it goes out of scope

#ifndef ZmAlloc_HH
#define ZmAlloc_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmStackAvail.hh>

template <typename T, typename VHeap = ZuVoid>
struct ZmAlloc_ {
  T *data = nullptr;

  ZmAlloc_() = default;
  ZmAlloc_(T *data_) : data{data_} { }
  ZmAlloc_(const ZmAlloc_ &) = delete;
  ZmAlloc_ &operator =(const ZmAlloc_ &) = delete;
  ZmAlloc_(ZmAlloc_ &&a) : data{a.data} { a.data = nullptr; }
  ZmAlloc_ &operator =(ZmAlloc_ &&a) = delete;
  ~ZmAlloc_() {
    if (ZuUnlikely(!data)) return;
    uint8_t *ptr_ = reinterpret_cast<uint8_t *>(data);
    auto self = ZmSelf();
    auto addr = reinterpret_cast<uint8_t *>(self->stackAddr());
    auto size = self->stackSize();
    if (ZuLikely(ptr_ >= addr && ptr_ < (addr + size)))
      ++self->m_allocStack;
    else {
      ++self->m_allocHeap;
      if constexpr (!ZuIsSame<VHeap, ZuVoid>{})
	VHeap::vfree(ptr_);
      else
	Zm::alignedFree(ptr_);
    }
  }

  ZuInline bool operator !() const { return !data; }
  ZuInline T &operator [](unsigned i) { return data[i]; }
  ZuInline const T &operator [](unsigned i) const { return data[i]; }
};

#define ZmAlloc_1(T, n) \
  ZmAlloc_<T>{static_cast<T *>(!(n) ? nullptr : \
    (((ZmStackAvail()>>1) < ((n) * sizeof(T) + alignof(T))) ? \
      Zm::alignedAlloc<alignof(T)>((n) * sizeof(T)) : \
	ZuAlloca((n) * sizeof(T), alignof(T))))}
// ZuDecay enables the caller to pass VHeap as `typename T::VHeap`
#define ZmAlloc_2(T, n, VHeap) \
  ZmAlloc_<T, VHeap>{static_cast<T *>(!(n) ? nullptr : \
    (((ZmStackAvail()>>1) < ((n) * sizeof(T) + alignof(T))) ? \
      ZuDecay<VHeap>::valloc((n) * sizeof(T)) : \
	ZuAlloca((n) * sizeof(T), alignof(T))))}
#define ZmAlloc_N(_0, _1, Fn, ...) Fn
#define ZmAlloc__(T, ...) \
  ZmAlloc_N(__VA_ARGS__, \
    ZmAlloc_2(T, __VA_ARGS__), \
    ZmAlloc_1(T, __VA_ARGS__))
// ZmAlloc() dependents call it using ZuPP_Eval
#define ZmAlloc(...) \
  ZuPP_Eval_(ZuPP_Defer(ZmAlloc__)(__VA_ARGS__))

#endif /* ZmAlloc_HH */
