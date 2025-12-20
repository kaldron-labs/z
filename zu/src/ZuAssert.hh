//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// compile-time assertion

#ifndef ZuAssert_HH
#define ZuAssert_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <assert.h>

#define ZuAssert(x) static_assert((x), #x)

// compile time C assert
#define ZuCAssert(x) switch (0) { case 0: case (x): ; }

#endif /* ZuAssert_HH */
