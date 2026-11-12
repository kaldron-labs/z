//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z AF_XDP library

#ifndef ZxdpLib_HH
#define ZxdpLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZXDP_EXPORTS
#define ZxdpAPI ZuExport_API
#define ZxdpExplicit ZuExport_Explicit
#else
#define ZxdpAPI ZuImport_API
#define ZxdpExplicit ZuImport_Explicit
#endif
#define ZxdpExtern extern ZxdpAPI

#else

#define ZxdpAPI ZuExport_API
#define ZxdpExplicit ZuExport_Explicit
#define ZxdpExtern extern ZxdpAPI

#endif

namespace Zxdp {

  inline constexpr const char Log[] = "Zxdp";

  ZxdpExtern void init(); // idempotent
  ZxdpExtern void warmup();

}

#endif /* ZxdpLib_HH */
