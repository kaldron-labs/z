//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Zv library main header

#ifndef ZvLib_HH
#define ZvLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZV_EXPORTS
#define ZvAPI ZuExport_API
#define ZvExplicit ZuExport_Explicit
#else
#define ZvAPI ZuImport_API
#define ZvExplicit ZuImport_Explicit
#endif
#define ZvExtern extern ZvAPI

#else

#define ZvAPI
#define ZvExplicit
#define ZvExtern extern

#endif

#endif /* ZvLib_HH */
