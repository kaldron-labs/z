//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC library

#ifndef ZquicLib_HH
#define ZquicLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZQUIC_EXPORTS
#define ZquicAPI ZuExport_API
#define ZquicExplicit ZuExport_Explicit
#else
#define ZquicAPI ZuImport_API
#define ZquicExplicit ZuImport_Explicit
#endif
#define ZquicExtern extern ZquicAPI

#else

#define ZquicAPI
#define ZquicExplicit
#define ZquicExtern extern

#endif

namespace Zquic {

inline constexpr const char Log[] = "Zquic";

ZquicExtern void init(); // idempotent

}

#endif /* ZquicLib_HH */
