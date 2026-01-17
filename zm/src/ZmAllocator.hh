//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// STL allocator using ZmHeap

#ifndef ZmAllocator_HH
#define ZmAllocator_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <new>
#include <memory>

#include <zlib/ZuTL.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmVHeap.hh>

#define ZmAllocator_DefltMin 8
#define ZmAllocator_DefltMax 1024

template <
  typename T,
  typename ID = ZuStringT<"ZmAllocator">,
  unsigned Min = ZmAllocator_DefltMin,
  unsigned Max = ZmAllocator_DefltMax,
  bool Sharded = false>
struct ZmAllocator_ {
  using size_type = std::size_t;
  using difference_type = ptrdiff_t;
  using pointer = T *;
  using const_pointer = const T *;
  using reference = T &;
  using const_reference = const T &;
  using value_type = T;

  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;
  using is_always_equal = std::true_type;

  ZmAllocator_() = default;
  ZmAllocator_(ZmAllocator_ &) = default;
  ZmAllocator_ &operator =(ZmAllocator_ &) = default;
  ZmAllocator_(ZmAllocator_ &&) = default;
  ZmAllocator_ &operator =(ZmAllocator_ &&) = default;
  ~ZmAllocator_() = default;

  template <typename U>
  constexpr ZmAllocator_(const ZmAllocator_<U, ID> &) { }
  template <typename U>
  ZmAllocator_ &operator =(const ZmAllocator_<U, ID> &) { return *this; }

  // despite prevalent myth, rebind is not deprecated for *custom*
  // allocators in C++17/20/23
  // - it is deprecated (and removed) from std::allocator because
  //   std::allocator_traits provides a more generic mechanism
  // - rebind is required for custom allocators like ZmAllocator
  //   that take non-type template parameters 
  // - see http://eel.is/c++draft/allocator.requirements.general#18
  template <typename U, typename ID_ = ID>
  struct rebind { using other = ZmAllocator_<U, ID_>; };

  T *allocate(std::size_t);
  void deallocate(T *, std::size_t);
};
template <
  typename T,
  ZuString ID = "ZmAllocator",
  unsigned Min = ZmAllocator_DefltMin,
  unsigned Max = ZmAllocator_DefltMax,
  bool Sharded = false>
using ZmAllocator = ZmAllocator_<T, ZuStringT<ID>, Min, Max, Sharded>;

template <typename T, typename ID, unsigned Min, unsigned Max, bool Sharded>
inline T *
ZmAllocator_<T, ID, Min, Max, Sharded>::allocate(std::size_t n) {
  using Cache =
    ZmHeapCacheT<ID, ZmHeapAllocSize<sizeof(T)>{}, alignof(T), Sharded>;

  using VHeap = ZmVHeap_<
    ID,
    sizeof(T) * Min,
    sizeof(T) * Max,
    alignof(T),
    Sharded>;

  if (ZuLikely(n == 1)) return static_cast<T *>(Cache::alloc());

  if (auto ptr = static_cast<T *>(VHeap::valloc(n * sizeof(T)))) return ptr;

  throw std::bad_alloc{};
}

template <typename T, typename ID, unsigned Min, unsigned Max, bool Sharded>
inline void
ZmAllocator_<T, ID, Min, Max, Sharded>::deallocate(T *p, std::size_t n) {
  using Cache =
    ZmHeapCacheT<ID, ZmHeapAllocSize<sizeof(T)>{}, alignof(T), Sharded>;

  using VHeap = ZmVHeap_<
    ID,
    sizeof(T) * Min,
    sizeof(T) * Max,
    alignof(T),
    Sharded>;

  if (ZuLikely(n == 1))
    Cache::free(p);
  else
    VHeap::vfree(p);
}

template <
  typename L, typename R,
  typename ID, unsigned Min, unsigned Max, bool Sharded>
constexpr bool operator ==(
  const ZmAllocator_<L, ID, Min, Max, Sharded> &,
  const ZmAllocator_<R, ID, Min, Max, Sharded> &)
{
  return true;
}

template <
  typename L, typename R,
  typename ID, unsigned Min, unsigned Max, bool Sharded>
constexpr bool operator !=(
  const ZmAllocator_<L, ID, Min, Max, Sharded> &,
  const ZmAllocator_<R, ID, Min, Max, Sharded> &)
{
  return false;
}

#endif /* ZmAllocator_HH */
