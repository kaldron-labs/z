//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Zcmd library main header

#ifndef ZcmdLib_HH
#define ZcmdLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZCMD_EXPORTS
#define ZcmdAPI ZuExport_API
#define ZcmdExplicit ZuExport_Explicit
#else
#define ZcmdAPI ZuImport_API
#define ZcmdExplicit ZuImport_Explicit
#endif
#define ZcmdExtern extern ZcmdAPI

#else

#define ZcmdAPI
#define ZcmdExplicit
#define ZcmdExtern extern

#endif

#endif /* ZcmdLib_HH */
