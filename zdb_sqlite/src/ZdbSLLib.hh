//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zdb SQLite library main header

#ifndef ZdbSLLib_HH
#define ZdbSLLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZDB_SL_EXPORTS
#define ZdbSLAPI ZuExport_API
#define ZdbSLExplicit ZuExport_Explicit
#else
#define ZdbSLAPI ZuImport_API
#define ZdbSLExplicit ZuImport_Explicit
#endif
#define ZdbSLExtern extern ZdbSLAPI

#else

#define ZdbSLAPI ZuExport_API
#define ZdbSLExplicit ZuExport_Explicit
#define ZdbSLExtern extern ZdbSLAPI

#endif

#endif /* ZdbSLLib_HH */
