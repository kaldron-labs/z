//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Zdb Postgres library main header

#ifndef ZdbPQLib_HH
#define ZdbPQLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZDB_PQ_EXPORTS
#define ZdbPQAPI ZuExport_API
#define ZdbPQExplicit ZuExport_Explicit
#else
#define ZdbPQAPI ZuImport_API
#define ZdbPQExplicit ZuImport_Explicit
#endif
#define ZdbPQExtern extern ZdbPQAPI

#else

#define ZdbPQAPI
#define ZdbPQExplicit
#define ZdbPQExtern extern

#endif

#endif /* ZdbPQLib_HH */
