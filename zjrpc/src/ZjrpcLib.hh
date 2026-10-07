//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z JSON-RPC library

#ifndef ZjrpcLib_HH
#define ZjrpcLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZJRPC_EXPORTS
#define ZjrpcAPI ZuExport_API
#define ZjrpcExplicit ZuExport_Explicit
#else
#define ZjrpcAPI ZuImport_API
#define ZjrpcExplicit ZuImport_Explicit
#endif
#define ZjrpcExtern extern ZjrpcAPI

#else

#define ZjrpcAPI ZuExport_API
#define ZjrpcExplicit ZuExport_Explicit
#define ZjrpcExtern extern ZjrpcAPI

#endif

#endif /* ZjrpcLib_HH */
