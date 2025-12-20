//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z Command Line library main header

#ifndef ZrlLib_HH
#define ZrlLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZRL_EXPORTS
#define ZrlAPI ZuExport_API
#define ZrlExplicit ZuExport_Explicit
#else
#define ZrlAPI ZuImport_API
#define ZrlExplicit ZuImport_Explicit
#endif
#define ZrlExtern extern ZrlAPI

#else

#define ZrlAPI
#define ZrlExplicit
#define ZrlExtern extern

#endif

#endif /* ZrlLib_HH */
