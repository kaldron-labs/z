//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// FIX format library

#ifndef ZfixFmtLib_HH
#define ZfixFmtLib_HH

#include <zlib/ZuLib.hh>

#if defined(_WIN32) && !defined(ZFIXFMT_EXPORTS)
#define ZfixFmtAPI ZuImport_API
#define ZfixFmtExplicit ZuImport_Explicit
#else
#define ZfixFmtAPI ZuExport_API
#define ZfixFmtExplicit ZuExport_Explicit
#endif
#define ZfixFmtExtern extern ZfixFmtAPI

ZfixFmtExtern const char ZfixFmtLib[];

#endif /* ZfixFmtLib_HH */
