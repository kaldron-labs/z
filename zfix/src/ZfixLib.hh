//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// FIX session library

#ifndef ZfixLib_HH
#define ZfixLib_HH

#include <zlib/ZuLib.hh>

#if defined(_WIN32) && !defined(ZFIX_EXPORTS)
#define ZfixAPI ZuImport_API
#define ZfixExplicit ZuImport_Explicit
#else
#define ZfixAPI ZuExport_API
#define ZfixExplicit ZuExport_Explicit
#endif
#define ZfixExtern extern ZfixAPI

ZfixExtern const char ZfixLib[];

#endif /* ZfixLib_HH */
