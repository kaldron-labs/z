//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// C99 standard integer types

#ifndef ZuInt_HH
#define ZuInt_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

using int128_t = __int128_t;
using uint128_t = __uint128_t;

template <int128_t I> using ZuInt128 = ZuConstant<int128_t, I>;
template <uint128_t I> using ZuUInt128 = ZuConstant<uint128_t, I>;

#endif /* ZuInt_HH */
