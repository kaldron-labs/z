//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z DataFrame library main header

#ifndef ZdfLib_HH
#define ZdfLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZDF_EXPORTS
#define ZdfAPI ZuExport_API
#define ZdfExplicit ZuExport_Explicit
#else
#define ZdfAPI ZuImport_API
#define ZdfExplicit ZuImport_Explicit
#endif
#define ZdfExtern extern ZdfAPI

#else

#define ZdfAPI
#define ZdfExplicit
#define ZdfExtern extern

#endif

#endif /* ZdfLib_HH */
