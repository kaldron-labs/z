//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z User Management library main header

#ifndef ZumLib_HH
#define ZumLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZV_EXPORTS
#define ZumAPI ZuExport_API
#define ZumExplicit ZuExport_Explicit
#else
#define ZumAPI ZuImport_API
#define ZumExplicit ZuImport_Explicit
#endif
#define ZumExtern extern ZumAPI

#else

#define ZumAPI
#define ZumExplicit
#define ZumExtern extern

#endif

#endif /* ZumLib_HH */
