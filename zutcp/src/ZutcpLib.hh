//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC library

#ifndef ZutcpLib_HH
#define ZutcpLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZUTCP_EXPORTS
#define ZutcpAPI ZuExport_API
#define ZutcpExplicit ZuExport_Explicit
#else
#define ZutcpAPI ZuImport_API
#define ZutcpExplicit ZuImport_Explicit
#endif
#define ZutcpExtern extern ZutcpAPI

#else

#define ZutcpAPI ZuExport_API
#define ZutcpExplicit ZuExport_Explicit
#define ZutcpExtern extern ZutcpAPI

#endif

namespace Zutcp {

  inline constexpr const char Log[] = "Zutcp";


}

#endif /* ZutcpLib_HH */
