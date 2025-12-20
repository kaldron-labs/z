//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z websockets library

#ifndef ZwsLib_HH
#define ZwsLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZWS_EXPORTS
#define ZwsAPI ZuExport_API
#define ZwsExplicit ZuExport_Explicit
#else
#define ZwsAPI ZuImport_API
#define ZwsExplicit ZuImport_Explicit
#endif
#define ZwsExtern extern ZwsAPI

#else

#define ZwsAPI
#define ZwsExplicit
#define ZwsExtern extern

#endif

#endif /* ZwsLib_HH */
