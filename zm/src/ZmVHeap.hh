//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// variable-sized recycling block allocator
// - designed and implemented with two goals:
//   - good performance, meeting or exceeding malloc/free
//   - run-time instrumentation
// - recycling free lists within fixed arenas
// - run-time instrumentation for heap analysis and tuning

#ifndef ZmVHeap_HH
#define ZmVHeap_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuSwitch.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmHeap.hh>

constexpr unsigned ZmVHeap_DefltMin = (1U<<10); // 1k
constexpr unsigned ZmVHeap_DefltMax = (1U<<20); // 1M

template <
  typename ID,
  unsigned Min = ZmVHeap_DefltMin,
  unsigned Max = ZmVHeap_DefltMax,
  unsigned Align = 1,
  bool Sharded = false>
class ZmVHeap_;

inline uint64_t ZmGrow(uint64_t o, uint64_t n)
{
  if (ZuUnlikely(o >= n)) return o;
  uint64_t i = n <= 1 ? 0 : (sizeof(n)<<3) - ZuIntrin::clz(n - 1);
  if (ZuUnlikely(i >= 17)) return ((n + 0xffffU) & ~0xffffU);
  return ZuSwitch::dispatch<17>(i, [](auto I) -> uint64_t {
    return ZmHeapAllocSize<(1U<<I)>{};
  });
}

template <
  typename ID,
  unsigned Min_,
  unsigned Max_,
  unsigned Align,
  bool Sharded>
class ZmVHeap_ {
public:
  ZuAssert(Align > 0);
  static constexpr unsigned HardMin = ZmHeapAllocSize<1>{};
  static constexpr unsigned Min = Min_ < HardMin ? HardMin : Min_;
  static constexpr unsigned Max = Max_ <= Min ? Min + 1 : Max_;

  static constexpr unsigned MinBits =
    ((sizeof(Min)<<3) - ZuIntrin::clz(Min - 1));
  static constexpr unsigned MaxBits_ =
    ((sizeof(Max)<<3) - ZuIntrin::clz(Max - 1));
  static constexpr unsigned MaxBits =
    MaxBits_ <= MinBits ? (MinBits + 1) :
      MaxBits_ > (MinBits + 17) ? (MinBits + 17) : MaxBits_;
  static constexpr unsigned NCaches = MaxBits - MinBits;

  static constexpr unsigned cacheI(size_t size) {
    // Sized allocations have no prefix to enforce the aligned block stride.
    if (size < Align) size = Align;
    if (size <= Min) return 0;
    return (sizeof(size)<<3) - ZuIntrin::clz(size - 1) - MinBits;
  }
  static constexpr unsigned blockSize(unsigned i) {
    return (uint64_t(1)<<(i + MinBits));
  }

  template <unsigned I>
  using Cache = ZmHeapCacheT<
    ID, ZmHeapAllocSize<blockSize(I)>{}, Align, Sharded, I>;

  // Known-size callers need no in-band size-class prefix. The matching free
  // takes the original payload size, including the old size after a resize.
  static void *alloc(size_t size) {
    if (ZuUnlikely(!size)) return nullptr;
    return alloc_(size, cacheI(size));
  }
  static void free(const void *ptr, size_t size) {
    if (ZuUnlikely(!ptr)) return;
    free_(ptr, cacheI(size));
  }

  static void *valloc(size_t size) {
    if (ZuUnlikely(!size)) return nullptr;
    size += Align;
    unsigned i = cacheI(size);
    auto ptr = static_cast<uint8_t *>(alloc_(size, i));
    *ptr = i;
    return ptr + Align;
  }
  static void vfree(const void *ptr_) {
    if (ZuUnlikely(!ptr_)) return;
    auto ptr = reinterpret_cast<const uint8_t *>(ptr_) - Align;
    free_(ptr, *ptr);
  }

private:
  static void *alloc_(size_t size, unsigned i) {
    if (!NCaches || ZuUnlikely(i >= NCaches)) { // jumbo
      void *ptr;
  retry:
      if (ZuLikely(ptr = Zm::alignedAlloc<Align>(size))) return ptr;
      if (ZmHeapFail()) goto retry;
      ZuUnreachable();
    } else if constexpr (NCaches) {
      return ZuSwitch::dispatch<NCaches>(i, [](auto I) -> void * {
	return Cache<I>::alloc();
      });
    }
  }
  static void free_(const void *ptr, unsigned i) {
    if (!NCaches || ZuUnlikely(i >= NCaches)) {
      Zm::alignedFree(ptr);
      return;
    } else if constexpr (NCaches) {
      ZuSwitch::dispatch<NCaches>(i, [ptr](auto I) {
	Cache<I>::free(const_cast<void *>(ptr));
      });
    }
  }
};
template <
  ZuString ID,
  unsigned Min = ZmVHeap_DefltMin,
  unsigned Max = ZmVHeap_DefltMax,
  unsigned Align = 1,
  bool Sharded = false>
using ZmVHeap = ZmVHeap_<ZuStringT<ID>, Min, Max, Align, Sharded>;

#endif /* ZmVHeap_HH */
