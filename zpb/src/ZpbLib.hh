//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z Protocol Buffers library main header

#ifndef ZpbLib_HH
#define ZpbLib_HH

#include <zlib/ZiLib.hh>

#ifdef _WIN32
#ifdef ZPB_EXPORTS
#define ZpbAPI ZuExport_API
#define ZpbExplicit ZuExport_Explicit
#else
#define ZpbAPI ZuImport_API
#define ZpbExplicit ZuImport_Explicit
#endif
#else
#define ZpbAPI ZuExport_API
#define ZpbExplicit ZuExport_Explicit
#endif
#define ZpbExtern extern ZpbAPI

#endif /* ZpbLib_HH */
