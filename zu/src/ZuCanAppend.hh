//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// compile-time ZuCanAppend<T> to test for T::append(U *, unsigned)

#ifndef ZuCanAppend_HH
#define ZuCanAppend_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

template <typename U, typename> struct ZuCanAppend_;
template <typename U> struct ZuCanAppend_<U, void> { using T = void; };
template <typename U> struct ZuCanAppend_<U, U &> { using T = void; };
template <typename U, typename T, typename = void>
struct ZuCanAppend : public ZuFalse { };
template <typename U, typename T>
struct ZuCanAppend<U, T,
  typename ZuCanAppend_<U,
    decltype(ZuDeclVal<ZuDeref<U> &>().append(ZuDeclVal<const T *>(), 1))>::T> :
  public ZuTrue { };

#endif /* ZuCanAppend_HH */
