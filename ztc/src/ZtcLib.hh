//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z telemetry and control library main header

#ifndef ZtcLib_HH
#define ZtcLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZTC_EXPORTS
#define ZtcAPI ZuExport_API
#define ZtcExplicit ZuExport_Explicit
#else
#define ZtcAPI ZuImport_API
#define ZtcExplicit ZuImport_Explicit
#endif
#define ZtcExtern extern ZtcAPI

#else

#define ZtcAPI
#define ZtcExplicit
#define ZtcExtern extern

#endif

#endif /* ZtcLib_HH */
