//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z DB library main header

#ifndef ZdbLib_HH
#define ZdbLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZDB_EXPORTS
#define ZdbAPI ZuExport_API
#define ZdbExplicit ZuExport_Explicit
#else
#define ZdbAPI ZuImport_API
#define ZdbExplicit ZuImport_Explicit
#endif
#define ZdbExtern extern ZdbAPI

#else

#define ZdbAPI
#define ZdbExplicit
#define ZdbExtern extern

#endif

#endif /* ZdbLib_HH */
