//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// extends ZtArray<> with an initial stack-allocated buffer (see ZmAlloc)
// - falls back to heap allocation if stack space is insufficient
// - auto var = ZtLocalArray(T, size);		// initialized empty
// - auto var = ZtLocalArray(T, length, size);	// initialized with length

#ifndef ZtLocalArray_HH
#define ZtLocalArray_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZmAlloc.hh>

#include <zlib/ZtArray.hh>

template <typename Array>
struct ZtLocalArray_ : private ZmAlloc_<typename Array::T>, public Array {
  using T = typename Array::T;
  using Array::data;
  using Array::operator [];
  using Array::operator !;
  ZuOpBool

  ZtLocalArray_(ZmAlloc_<T> buf, unsigned size) :
    ZmAlloc_<T>(ZuMv(buf)),
    Array(ZmAlloc_<T>::ptr, 0, size, false)
  {
    auto ptr = ZmAlloc_<T>::ptr;
    if (ZuUnlikely(!ptr))
      Array::null_();
  }
  ZtLocalArray_(
    ZmAlloc_<T> buf, unsigned length, unsigned size,
    bool initElems = !ZuTraits<T>::IsPrimitive) :
    ZmAlloc_<T>(ZuMv(buf)),
    Array(ZmAlloc_<T>::ptr, length, size, false)
  {
    auto ptr = ZmAlloc_<T>::ptr;
    if (ZuUnlikely(!ptr))
      Array::null_();
    else if (length && initElems)
      Array::initElems(ptr, length);
  }
};

#define ZtLocalArray_1(A, size) \
  ZtLocalArray_<A>(ZmAlloc(typename A::T, size), size)
#define ZtLocalArray_2(A, length, size) \
  ZtLocalArray_<A>(ZmAlloc(typename A::T, size), length, size)
#define ZtLocalArray_N(_0, _1, Fn, ...) Fn
#define ZtLocalArray__(A, ...) \
  ZtLocalArray_N(__VA_ARGS__, \
    ZtLocalArray_2(A, __VA_ARGS__), \
    ZtLocalArray_1(A, __VA_ARGS__))
#define ZtLocalArray(...) \
  ZuPP_Eval(ZuPP_Defer(ZtLocalArray__)(__VA_ARGS__))

#endif /* ZtLocalArray_HH */
