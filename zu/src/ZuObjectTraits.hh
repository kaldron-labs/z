//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// compile-time traits for intrusively reference-counted objects

#ifndef ZuObjectTraits_HH
#define ZuObjectTraits_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

void ZuObjectType(...);

template <
  typename U,
  typename O = decltype(ZuObjectType(ZuDeclVal<ZuDecay<U> *>()))>
struct ZuObjectTraits {
  using T = O;
  enum { IsObject = 1 };
};
template <typename U>
struct ZuObjectTraits<U, void> {
  enum { IsObject = 0 };
};
template <typename U>
using ZuIsObject = ZuBool<ZuObjectTraits<U>::IsObject>;
template <typename U, typename R = void>
using ZuMatchObject = ZuIfT<ZuObjectTraits<U>::IsObject, R>;
template <typename U, typename R = void>
using ZuNotObject = ZuIfT<!ZuObjectTraits<U>::IsObject, R>;

#endif /* ZuObjectTraits_HH */
