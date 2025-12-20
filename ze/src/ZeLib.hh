//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z Error library main header

#ifndef ZeLib_HH
#define ZeLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZE_EXPORTS
#define ZeAPI ZuExport_API
#define ZeExplicit ZuExport_Explicit
#else
#define ZeAPI ZuImport_API
#define ZeExplicit ZuImport_Explicit
#endif
#define ZeExtern extern ZeAPI

#else

#define ZeAPI
#define ZeExplicit
#define ZeExtern extern

#endif

#endif /* ZeLib_HH */
