//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// compile-time determination of maximum sizeof()
//
// ZuLargest<Ts...> evaluates to the largest T,
// returning the first listed in the event of a tie

#ifndef ZuLargest_HH
#define ZuLargest_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuTL.hh>

template <typename ...Ts> struct ZuLargest_;
template <> struct ZuLargest_<> { using T = void; };
template <typename T0> struct ZuLargest_<T0> { using T = T0; };
template <typename T0, typename ...Ts>
struct ZuLargest_<T0, Ts...> {
  using T =
    ZuIf<(unsigned(ZuSize<T0>{}) >=
	  unsigned(ZuSize<typename ZuLargest_<Ts...>::T>{})),
      T0, typename ZuLargest_<Ts...>::T>;
};
template <typename ...Ts>
struct ZuLargest_<void, Ts...> {
  using T = typename ZuLargest_<Ts...>::T;
};
template <typename ...Ts>
struct ZuLargest_<ZuTypeList<Ts...>> : public ZuLargest_<Ts...> { };
template <typename ...Ts>
using ZuLargest = typename ZuLargest_<Ts...>::T;

#endif /* ZuLargest_HH */
