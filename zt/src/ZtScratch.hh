//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// extends ZtArray/ZtString with stack-allocated backing storage (see ZmAlloc)
// - falls back to heap allocation if stack space is insufficient
// - auto var = ZtScratch(T, size);		// initialized empty
// - auto var = ZtScratch(T, length, size);	// initialized with length

#ifndef ZtScratch_HH
#define ZtScratch_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTraits.hh>

#include <zlib/ZmAlloc.hh>

#include <zlib/ZtArray.hh>

template <typename U, typename = void>
struct ZtScratch_CanInit : public ZuFalse { };
template <typename U>
struct ZtScratch_CanInit<U, decltype(
  ZuDecay<U>::initElems(
    ZuDeclVal<typename ZuTraits<ZuDecay<U>>::Elem *>(),
    ZuDeclVal<uint64_t>()), void())> :
  public ZuTrue { };

template <typename Array>
struct ZtScratch_ :
  private ZmAlloc_<typename ZuTraits<Array>::Elem, typename Array::VHeap>,
  public Array
{
  using T = typename ZuTraits<Array>::Elem;
private:
  using VHeap = typename Array::VHeap;
  using VAlloc = ZmAlloc_<T, VHeap>;
public:
  using Array::data;
  using Array::operator [];
  using Array::operator !;
  ZuOpBool

  ZtScratch_(VAlloc buf, unsigned size) :
    VAlloc(ZuMv(buf)),
    Array(VAlloc::data, 0, size, false) { }
  template <typename U = Array,
    ZuIfT<ZtScratch_CanInit<U>{}, int> = 0>
  ZtScratch_(
    VAlloc buf, unsigned length, unsigned size,
    bool initElems = !ZuTraits<T>::IsPrimitive) :
    VAlloc(ZuMv(buf)),
    Array(VAlloc::data, length, size, false)
  {
    if (VAlloc::data && length && initElems)
      Array::initElems(VAlloc::data, length);
  }
  template <typename U = Array,
    ZuIfT<!ZtScratch_CanInit<U>{}, int> = 0>
  ZtScratch_(VAlloc buf, unsigned length, unsigned size) :
    VAlloc(ZuMv(buf)),
    Array(VAlloc::data, length, size, false) { }
};

#define ZtScratch_1(A, size) \
  ZtScratch_<A>( \
    ZmAlloc(typename ZuTraits<A>::Elem, size, typename A::VHeap), size)
#define ZtScratch_2(A, length, size) \
  ZtScratch_<A>( \
    ZmAlloc(typename ZuTraits<A>::Elem, size, typename A::VHeap), length, size)
#define ZtScratch_N(_0, _1, Fn, ...) Fn
#define ZtScratch__(A, ...) \
  ZtScratch_N(__VA_ARGS__, \
    ZtScratch_2(A, __VA_ARGS__), \
    ZtScratch_1(A, __VA_ARGS__))
#define ZtScratch(...) \
  ZuPP_Eval(ZuPP_Defer(ZtScratch__)(__VA_ARGS__))

#endif /* ZtScratch_HH */
