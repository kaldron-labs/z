//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z Type library main header

#ifndef ZtLib_HH
#define ZtLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZT_EXPORTS
#define ZtAPI ZuExport_API
#define ZtExplicit ZuExport_Explicit
#else
#define ZtAPI ZuImport_API
#define ZtExplicit ZuImport_Explicit
#endif
#define ZtExtern extern ZtAPI

#else

#define ZtAPI
#define ZtExplicit
#define ZtExtern extern

#endif

#endif /* ZtLib_HH */
