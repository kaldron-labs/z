//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z data format library main header

#ifndef ZfLib_HH
#define ZfLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZF_EXPORTS
#define ZfAPI ZuExport_API
#define ZfExplicit ZuExport_Explicit
#else
#define ZfAPI ZuImport_API
#define ZfExplicit ZuImport_Explicit
#endif
#define ZfExtern extern ZfAPI

#else

#define ZfAPI
#define ZfExplicit
#define ZfExtern extern

#endif

#endif /* ZfLib_HH */
