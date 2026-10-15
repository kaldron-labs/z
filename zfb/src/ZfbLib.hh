//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z FlatBuffers library main header

#ifndef ZfbLib_HH
#define ZfbLib_HH

#include <zlib/ZiLib.hh>

#ifdef _WIN32
#ifdef ZFB_EXPORTS
#define ZfbAPI ZuExport_API
#define ZfbExplicit ZuExport_Explicit
#else
#define ZfbAPI ZuImport_API
#define ZfbExplicit ZuImport_Explicit
#endif
#else
#define ZfbAPI ZuExport_API
#define ZfbExplicit ZuExport_Explicit
#endif
#define ZfbExtern extern ZfbAPI

#endif /* ZfbLib_HH */
