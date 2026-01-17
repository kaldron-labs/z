//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// type equivalence, normalizing character types with ZuNorm

#ifndef ZuEquiv_HH
#define ZuEquiv_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuNorm.hh>

template <typename U1, typename U2>
struct ZuEquiv : public ZuIsSame<ZuNorm<U1>, ZuNorm<U2>> { };

#endif /* ZuEquiv_HH */
