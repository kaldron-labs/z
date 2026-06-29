//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// structural element wrapper for ZuArray (and ZuSpan)
// - permits compile-time uninitialized data

#ifndef ZuElem_HH
#define ZuElem_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

template <typename T>
struct alignas(T) ZuElem {
  ZuInline constexpr ZuElem() noexcept { }
  ZuInline constexpr ~ZuElem() noexcept { }
  union { T v; };
};

template <typename> struct ZuIsElem_ : public ZuFalse { };
template <typename U> struct ZuIsElem_<ZuElem<U>> : public ZuTrue { };
template <typename U> using ZuIsElem = ZuIsElem_<ZuDecay<U>>;

template <typename U> struct ZuElemType_ { using T = U; };
template <typename U> struct ZuElemType_<ZuElem<U>> { using T = U; };
template <typename U> using ZuElemType = typename ZuElemType_<ZuDecay<U>>::T;

template <typename U>
ZuInline constexpr decltype(auto) ZuElemVal(U &&u) {
  if constexpr (ZuIsElem<U>{})
    return ZuFwdLike<U>(u.v);
  else
    return ZuFwd<U>(u);
}

#endif /* ZuElem_HH */
