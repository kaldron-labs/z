//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// safe large object allocation on stack, falling back to heap
// - uses ZuAlloca if sufficient stack space is available
//
// auto x = ZmLocal(Type, ...);

#ifndef ZmLocal_HH
#define ZmLocal_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmStackAvail.hh>

template <typename T>
struct ZmLocal_ {
  T *ptr = nullptr;

  ZmLocal_() = default;
  template <typename ...Args,
    decltype(T(ZuDeclVal<Args &&>()...), int()) = 0>
  ZmLocal_(T *ptr_, Args &&...args) : ptr{ptr_} {
    new (ptr) T(ZuFwd<Args>(args)...);
  }
  ZmLocal_(const ZmLocal_ &) = delete;
  ZmLocal_ &operator =(const ZmLocal_ &) = delete;
  ZmLocal_(ZmLocal_ &&a) : ptr{a.ptr} { a.ptr = nullptr; }
  ZmLocal_ &operator =(ZmLocal_ &&a) {
    ptr = a.ptr;
    a.ptr = nullptr;
    return *this;
  }
  ~ZmLocal_() {
    if (ZuUnlikely(!ptr)) return;
    if constexpr (ZuTraits<T>::IsComposite) ptr->~T();
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

  ZuInline operator T *() const { return ptr; }
  ZuInline T *operator ->() const { return ptr; }
};

#define ZmLocal(T, ...) \
  ZmLocal_<T>{static_cast<T *>( \
    (((ZmStackAvail()>>1) < (sizeof(T) + alignof(T))) ? \
      Zm::alignedAlloc<alignof(T)>(sizeof(T)) : \
	ZuAlloca(sizeof(T), alignof(T)))) __VA_OPT__(, __VA_ARGS__)}

#endif /* ZmLocal_HH */
