//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// alternate type mapping
// - primary use-case is char <-> wchar_t

#ifndef ZuAlt_HH
#define ZuAlt_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuNorm.hh>

template <typename U>
using ZuAlt =
  ZuIf<ZuIsSame<ZuNorm<U>, char>{}, wchar_t,
  ZuIf<ZuIsSame<ZuNorm<U>, wchar_t>{}, char, void>>;

#endif /* ZuAlt_HH */
