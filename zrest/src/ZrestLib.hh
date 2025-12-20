//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z REST library

#ifndef ZrestLib_HH
#define ZrestLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZREST_EXPORTS
#define ZrestAPI ZuExport_API
#define ZrestExplicit ZuExport_Explicit
#else
#define ZrestAPI ZuImport_API
#define ZrestExplicit ZuImport_Explicit
#endif
#define ZrestExtern extern ZrestAPI

#else

#define ZrestAPI
#define ZrestExplicit
#define ZrestExtern extern

#endif

#endif /* ZrestLib_HH */
