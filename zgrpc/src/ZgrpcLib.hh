//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z gRPC library

#ifndef ZgrpcLib_HH
#define ZgrpcLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZGRPC_EXPORTS
#define ZgrpcAPI ZuExport_API
#define ZgrpcExplicit ZuExport_Explicit
#else
#define ZgrpcAPI ZuImport_API
#define ZgrpcExplicit ZuImport_Explicit
#endif
#define ZgrpcExtern extern ZgrpcAPI

#else

#define ZgrpcAPI ZuExport_API
#define ZgrpcExplicit ZuExport_Explicit
#define ZgrpcExtern extern ZgrpcAPI

#endif

namespace Zgrpc {
  inline constexpr const char Log[] = "Zgrpc";
}

#endif /* ZgrpcLib_HH */
