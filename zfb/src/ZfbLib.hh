//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z flatbuffers Library

#ifndef ZfbLib_HH
#define ZfbLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZFB_EXPORTS
#define ZfbAPI ZuExport_API
#define ZfbExplicit ZuExport_Explicit
#else
#define ZfbAPI ZuImport_API
#define ZfbExplicit ZuImport_Explicit
#endif
#define ZfbExtern extern ZfbAPI

#else

#define ZfbAPI
#define ZfbExplicit
#define ZfbExtern extern

#endif

#endif /* ZfbLib_HH */
