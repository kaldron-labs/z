//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library

#ifndef ZhttpLib_HH
#define ZhttpLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZHTTP_EXPORTS
#define ZhttpAPI ZuExport_API
#define ZhttpExplicit ZuExport_Explicit
#else
#define ZhttpAPI ZuImport_API
#define ZhttpExplicit ZuImport_Explicit
#endif
#define ZhttpExtern extern ZhttpAPI

#else

#define ZhttpAPI
#define ZhttpExplicit
#define ZhttpExtern extern

#endif

#endif /* ZhttpLib_HH */
