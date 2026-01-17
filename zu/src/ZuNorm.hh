//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// type normalization
// - principal use case is normalizing char types:
//   - char/signed char/unsigned char/int8_t/uint8_t -> char
//   - wchar_t/short/unsigned short/int16_t/uint16_t -> wchar_t
// - can be extended to other situations where multiple distinct types are
//   fungible and have identical in-memory representations
//
// Note: 16bit types are left as-is if wchar_t is not 16bit

#ifndef ZuNorm_HH
#define ZuNorm_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <wchar.h>

#include <zlib/ZuInt.hh>

template <typename U, typename W = wchar_t,
  bool = bool(ZuIsSame<U, char>{}) ||
	 bool(ZuIsSame<U, signed char>{}) ||
	 bool(ZuIsSame<U, unsigned char>{}) ||
	 bool(ZuIsSame<U, int8_t>{}) ||
	 bool(ZuIsSame<U, uint8_t>{}),
  bool = bool(ZuIsSame<U, W>{}) ||
	 (sizeof(W) == 2 && (
	       bool(ZuIsSame<U, short>{}) ||
	       bool(ZuIsSame<U, unsigned short>{}) ||
	       bool(ZuIsSame<U, int16_t>{}) ||
	       bool(ZuIsSame<U, uint16_t>{}))) ||
	 (sizeof(W) == 4 && (
	       bool(ZuIsSame<U, int32_t>{}) ||
	       bool(ZuIsSame<U, uint32_t>{})))>
struct ZuNorm_ { using T = U; };

template <typename U, typename W, bool _>
struct ZuNorm_<U, W, 1, _> { using T = char; };

template <typename U, typename W>
struct ZuNorm_<U, W, 0, 1> { using T = W; };

template <typename U>
using ZuNorm = typename ZuNorm_<ZuDecay<U>>::T;

#endif /* ZuNorm_HH */
