//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// fast generic array operations

// ZuArrayFn<T> provides fast array operations used
// by other templates and classes

#ifndef ZuArrayFn_HH
#define ZuArrayFn_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>

// array initialization / destruction / move / copy operations
// - generally equivalent to std::copy etc.

template <typename T, class Cmp, bool IsPrimitive> struct ZuArrayFn_ElemOps_;

template <typename T, class Cmp>
struct ZuArrayFn_ElemOps_<T, Cmp, false> {
  template <
    typename V = T,
    decltype(V(), int()) = 0,
    bool NoExcept = noexcept(V())>
  ZuInline static constexpr void initElem(T *dst) noexcept(NoExcept) {
    if (ZuConstEval())
      ZuNew<T>(dst);
    else
      new (dst) T();
  }
  template <
    typename P, typename V = T,
    decltype(V(ZuDeclVal<P &&>()), int()) = 0,
    bool NoExcept = noexcept(V(ZuDeclVal<P &&>()))>
  ZuInline static constexpr void initElem(T *dst, P &&p) noexcept(NoExcept) {
    if (ZuConstEval())
      ZuNew<T>(dst, ZuFwd<P>(p));
    else
      new (dst) T(ZuFwd<P>(p));
  }
  template <
    typename V = T,
    decltype(ZuDeclVal<V &>().~V(), int()) = 0,
    bool NoExcept = noexcept(ZuDeclVal<V &>().~V())>
  ZuInline static constexpr void destroyElem(T *dst) { (*dst).~T(); }
};

template <typename T, class Cmp>
struct ZuArrayFn_ElemOps_<T, Cmp, true> {
  ZuInline static constexpr void initElem(T *dst) noexcept {
    *dst = Cmp::null();
  }
  template <typename P>
  ZuInline static constexpr void initElem(T *dst, P &&p) noexcept {
    *dst = ZuFwd<P>(p);
  }
  ZuInline static constexpr void destroyElem(T *dst) noexcept { }
};

template <typename T, class Cmp>
struct ZuArrayFn_ElemOps :
    public ZuArrayFn_ElemOps_<T, Cmp, ZuTraits<T>::IsPrimitive> {
  using Base = ZuArrayFn_ElemOps_<T, Cmp, ZuTraits<T>::IsPrimitive>;
  using Base::initElem;

  template <
    typename V = T,
    decltype(initElem(static_cast<V *>(nullptr)), int()) = 0,
    bool NoExcept = noexcept(initElem(static_cast<V *>(nullptr)))>
  ZuInline static constexpr void initElems(T *dst, uint64_t length)
    noexcept(NoExcept)
  {
    if (ZuLikely(length))
      do { initElem(dst++); } while (--length > 0);
  }
};

template <typename T, class Cmp, bool IsPOD>
struct ZuArrayFn_Ops : public ZuArrayFn_ElemOps<T, Cmp> {
  ZuInline static constexpr void destroyElems(T *dst, uint64_t length)
    noexcept(ZuNXDestroy<T>{})
  {
    if (ZuLikely(length))
      do { (*dst++).~T(); } while (--length > 0);
  }

  template <
    typename S,
    typename V = T,
    decltype(V(ZuDeclVal<const S &>()), int()) = 0,
    bool NoExcept = noexcept(V(ZuDeclVal<const S &>()), int())>
  inline static constexpr void copyElems(
    T *dst, const S *src, uint64_t length) noexcept(NoExcept)
  {
    if (ZuUnlikely(!length)) return;
    if constexpr (ZuIsConvertible<S *, T *>{})
      if (ZuUnlikely(dst == static_cast<const T *>(src))) return;
    if (ZuConstEval())
      do { ZuNew<T>(dst++, *src++); } while (--length > 0);
    else
      do { new (dst++) T(*src++); } while (--length > 0);
  }

  template <
    bool Destroy = true,
    typename S,
    typename V = T,
    decltype(V(ZuDeclVal<S &&>()), int()) = 0,
    bool NoExcept = noexcept(V(ZuDeclVal<S &&>()), int())>
  inline static constexpr void moveElems(
    T *dst, S *src, uint64_t length) noexcept(NoExcept)
  {
    if (ZuConstEval()) {
      if (ZuUnlikely(!length)) return;
      if constexpr (ZuIsConvertible<S *, T *>{})
	if (ZuUnlikely(dst == static_cast<T *>(src))) return;
      do {
	ZuNew<T>(dst++, ZuMv(*src));
	if constexpr (Destroy) src->~T();
	++src;
      } while (--length > 0);
    } else {
      ptrdiff_t diff = 
	reinterpret_cast<const char *>(dst) -
	reinterpret_cast<const char *>(src);
      if (ZuUnlikely(!length || !diff)) return;
      if (diff < 0 || diff > ptrdiff_t(length * sizeof(T))) {
	do {
	  new (dst++) T(ZuMv(*src));
	  if constexpr (Destroy) src->~T();
	  ++src;
	} while (--length > 0);
      } else {
	dst += length;
	src += length;
	do {
	  new (--dst) T(ZuMv(*--src));
	  if constexpr (Destroy) src->~T();
	} while (--length > 0);
      }
    }
  }
};

template <typename T1, typename T2>
struct ZuArrayFn_POD {
  enum { Same = ZuIsSame<T1, T2>{} ||
    (sizeof(T1) == sizeof(T2) &&
     ZuTraits<T1>::IsIntegral && ZuTraits<T1>::IsPrimitive &&
     ZuTraits<T2>::IsIntegral && ZuTraits<T2>::IsPrimitive) };
};
template <typename T1, typename T2, typename R = void>
using ZuArrayFn_SamePOD = ZuIfT<ZuArrayFn_POD<T1, T2>::Same, R>;
template <typename T1, typename T2, typename R = void>
using ZuArrayFn_NotSamePOD = ZuIfT<!ZuArrayFn_POD<T1, T2>::Same, R>;

template <typename T, class Cmp>
class ZuArrayFn_Ops<T, Cmp, true> : public ZuArrayFn_ElemOps<T, Cmp> {
public:
  ZuInline static constexpr void
  destroyElems(T *dst, uint64_t length) noexcept { }

  template <
    typename S,
    typename V = T,
    ZuArrayFn_NotSamePOD<V, S, int> = 0,
    decltype(V(ZuDeclVal<const S &>()), int()) = 0,
    bool NoExcept = noexcept(V(ZuDeclVal<const S &>()), int())>
  inline static constexpr void copyElems(
    T *dst, const S *src, uint64_t length) noexcept(NoExcept)
  {
    if (ZuUnlikely(!length)) return;
    if constexpr (ZuIsConvertible<S *, T *>{})
      if (ZuUnlikely(dst == static_cast<const T *>(src))) return;
    if (ZuConstEval())
      do { ZuNew<T>(dst++, *src++); } while (--length > 0);
    else
      do { new (dst++) T(*src++); } while (--length > 0);
  }

  template <
    typename S,
    typename V = T,
    ZuArrayFn_SamePOD<V, S, int> = 0,
    decltype(V(ZuDeclVal<const S &>()), int()) = 0,
    bool NoExcept = noexcept(V(ZuDeclVal<const S &>()), int())>
  inline static constexpr void copyElems(
    T *dst, const S *src, uint64_t length) noexcept
  {
    if (ZuUnlikely(!length)) return;
    if constexpr (ZuIsConvertible<S *, T *>{})
      if (ZuUnlikely(dst == static_cast<const T *>(src))) return;
    if (ZuConstEval()) {
      do { ZuNew<T>(dst++, *src++); } while (--length > 0);
    } else {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#ifdef __llvm__
#pragma GCC diagnostic ignored "-Wnontrivial-memcall"
#endif
#endif
      memmove(dst, src, length * sizeof(T));
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    }
  }

  template <
    bool Destroy = true,
    typename S,
    typename V = T,
    ZuArrayFn_NotSamePOD<V, S, int> = 0,
    decltype(V(ZuDeclVal<S &&>()), int()) = 0,
    bool NoExcept = noexcept(V(ZuDeclVal<S &&>()), int())>
  inline static constexpr void moveElems(
    T *dst, const S *src, uint64_t length) noexcept(NoExcept)
  {
    if (ZuUnlikely(!length)) return;
    if constexpr (ZuIsConvertible<S *, T *>{})
      if (ZuUnlikely(dst == static_cast<const T *>(src))) return;
    if (ZuConstEval()) {
      do {
	ZuNew<T>(dst++, ZuMv(*src));
	if constexpr (Destroy) src->~T();
	++src;
      } while (--length > 0);
    } else {
      if constexpr (!ZuIsConvertible<S *, T *>{}) {
	do {
	  new (dst++) T(ZuMv(*src));
	  if constexpr (Destroy) src->~T();
	  ++src;
	} while (--length > 0);
      } else if (static_cast<T *>(src) > dst ||
	  length < uint64_t(dst - static_cast<const T *>(src))) {
	do {
	  new (dst++) T(ZuMv(*src));
	  if constexpr (Destroy) src->~T();
	  ++src;
	} while (--length > 0);
      } else {
	dst += length;
	src += length;
	do {
	  new (--dst) T{ZuMv(*--src)};
	  if constexpr (Destroy) src->~T();
	} while (--length > 0);
      }
    }
  }
  template <
    bool Destroy = true,
    typename S,
    typename V = T,
    ZuArrayFn_SamePOD<V, S, int> = 0,
    decltype(V(ZuDeclVal<S &&>()), int()) = 0>
  inline static constexpr void moveElems(
    T *dst, const S *src, uint64_t length) noexcept
  {
    if (ZuConstEval()) {
      if (ZuUnlikely(!length)) return;
      if constexpr (ZuIsConvertible<S *, T *>{})
	if (ZuUnlikely(dst == static_cast<const T *>(src))) return;
      do {
	ZuNew<T>(dst++, ZuMv(*src));
	if constexpr (Destroy) src->~T();
	++src;
      } while (--length > 0);
    } else {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#ifdef __llvm__
#pragma GCC diagnostic ignored "-Wnontrivial-memcall"
#endif
#endif
      memmove(dst, src, length * sizeof(T));
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    }
  }
};

// array comparison

template <typename T, class Cmp, class DefltCmp, bool IsIntegralPOD>
struct ZuArrayFn_Cmp {
  ZuInline static constexpr int cmp(
    const T *dst, const T *src, uint64_t length)
  {
    while (ZuLikely(length--))
      if (int i = Cmp::cmp(*dst++, *src++)) return i;
    return 0;
  }
  ZuInline static constexpr bool equals(
    const T *dst, const T *src, uint64_t length)
  {
    while (ZuLikely(length--))
      if (!Cmp::equals(*dst++, *src++)) return false;
    return true;
  }
};

template <typename T, class Cmp> struct ZuArrayFn_Cmp<T, Cmp, Cmp, true> {
  ZuInline static constexpr int cmp(
    const T *dst, const T *src, uint64_t length)
  {
    if (ZuUnlikely(!length || dst == src)) return 0;
    if (ZuConstEval() || sizeof(T) > 1) {
      while (ZuLikely(length--))
	if (int i = Cmp::cmp(*dst++, *src++)) return i;
      return 0;
    } else {
      return memcmp(dst, src, length * sizeof(T));
    }
  }
  ZuInline static constexpr bool equals(
    const T *dst, const T *src, uint64_t length)
  {
    if (ZuUnlikely(!length || dst == src)) return true;
    if (ZuConstEval()) {
      while (ZuLikely(length--))
	if (!Cmp::equals(*dst++, *src++)) return false;
      return true;
    } else {
      return !memcmp(dst, src, length * sizeof(T));
    }
  }
};

template <typename T, bool = ZuEquiv<T, char>{} || ZuEquiv<T, wchar_t>{}>
struct ZuArrayFn_Hash {
  static uint32_t hash(const T *data, uint64_t length) {
    ZuHash_FNV::Value v = ZuHash_FNV::initial_();
    if (ZuLikely(length))
      do {
	v = ZuHash_FNV::hash_(v, ZuHash<T>::hash(*data++));
      } while (--length > 0);
    return (uint32_t)v;
  }
};
template <typename T>
struct ZuArrayFn_Hash<T, true> {
  ZuInline static uint32_t hash(const T *data, uint64_t length) {
    return ZuStringHash<T>::hash(data, length);
  }
};

// main template

template <typename T, class Cmp = ZuCmp<T>> class ZuArrayFn :
  public ZuArrayFn_Ops<T, Cmp, ZuTraits<T>::IsPOD>,
  public ZuArrayFn_Cmp<T, Cmp, ZuCmp<T>, ZuTraits<T>::IsPOD && ZuTraits<T>::IsIntegral>,
  public ZuArrayFn_Hash<T> { };

template <typename T> class ZuArrayFn_Null {
public:
  ZuInline static constexpr void initElem(T *dst) noexcept { }
  template <typename P>
  ZuInline static void initElem(T *dst, P &&p) noexcept { }
  ZuInline static constexpr void destroyElem(T *dst) noexcept { }
  ZuInline static constexpr void
  initElems(T *dst, uint64_t length) noexcept { }
  ZuInline static constexpr void
  destroyElems(T *dst, uint64_t length) noexcept { }
  template <typename S>
  ZuInline static constexpr void
  copyElems(T *dst, const S *src, uint64_t length) noexcept { }
  ZuInline static constexpr void
  moveElems(T *dst, const T *src, uint64_t length) noexcept { }
  ZuInline static constexpr int
  cmp(const T *dst, const T *src, uint64_t length) { return 0; }
  ZuInline static constexpr bool
  equals(const T *dst, const T *src, uint64_t length) { return true; }
  ZuInline static uint32_t
  hash(const T *data, uint64_t length) { return 0; }
};

template <typename Cmp>
class ZuArrayFn<void, Cmp> : public ZuArrayFn_Null<void> { };

#endif /* ZuArrayFn_HH */
