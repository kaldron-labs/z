//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// compile-time determination of maximum alignof()
//
// ZuMostAligned<Ts...> evaluates to whichever T has the greatest alignment,
// returning the first listed in the event of a tie

#ifndef ZuMostAligned_HH
#define ZuMostAligned_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuTL.hh>

template <typename ...Ts> struct ZuMostAligned_;
template <> struct ZuMostAligned_<> { using T = void; };
template <typename T0> struct ZuMostAligned_<T0> { using T = T0; };
template <typename T0, typename ...Ts>
struct ZuMostAligned_<T0, Ts...> {
  using T =
    ZuIf<(alignof(T0) >= alignof(typename ZuMostAligned_<Ts...>::T)),
      T0, typename ZuMostAligned_<Ts...>::T>;
};
template <typename ...Ts>
struct ZuMostAligned_<void, Ts...> {
  using T = typename ZuMostAligned_<Ts...>::T;
};
template <typename ...Ts>
struct ZuMostAligned_<ZuTypeList<Ts...>> : public ZuMostAligned_<Ts...> { };
template <typename ...Ts>
using ZuMostAligned = typename ZuMostAligned_<Ts...>::T;

#endif /* ZuMostAligned_HH */
