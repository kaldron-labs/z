//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z MCP library

#ifndef ZmcpLib_HH
#define ZmcpLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZMCP_EXPORTS
#define ZmcpAPI ZuExport_API
#define ZmcpExplicit ZuExport_Explicit
#else
#define ZmcpAPI ZuImport_API
#define ZmcpExplicit ZuImport_Explicit
#endif
#define ZmcpExtern extern ZmcpAPI

#else

#define ZmcpAPI
#define ZmcpExplicit
#define ZmcpExtern extern

#endif

#endif /* ZmcpLib_HH */
