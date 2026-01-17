//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// time-based one time password (Google Authenticator compatible)

#ifndef ZtlsTOTP_HH
#define ZtlsTOTP_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <stdlib.h>

#include <zlib/ZuByteSwap.hh>

#include <zlib/ZuTime.hh>

#include <zlib/ZtlsHMAC.hh>

namespace Ztls::TOTP {

ZtlsExtern unsigned calc(ZuBSpan, int offset = 0);
ZtlsExtern bool verify(ZuBSpan, unsigned code, unsigned range);

}

#endif /* ZtlsTOTP_HH */
