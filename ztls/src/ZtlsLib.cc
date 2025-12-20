//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z TLS Library - mbedtls C++ wrapper

#include <zlib/ZtlsLib.hh>

#include <zlib/ZmSingleton.hh>

#include <psa/crypto.h>
#include <mbedtls/platform.h>

ZtlsExtern const char ZtlsLib[] = "@(#) Z TLS Library v" Z_VERNAME;
