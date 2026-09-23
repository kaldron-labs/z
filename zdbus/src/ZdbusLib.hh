//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Linux D-Bus client library

#ifndef ZdbusLib_HH
#define ZdbusLib_HH

#ifndef __linux__
#error Zdbus is supported only on Linux
#endif

#include <zlib/ZuLib.hh>

#define ZdbusAPI ZuExport_API
#define ZdbusExtern extern ZdbusAPI

#endif /* ZdbusLib_HH */
