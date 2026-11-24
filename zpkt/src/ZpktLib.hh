//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z AF_XDP library

#ifndef ZpktLib_HH
#define ZpktLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZPKT_EXPORTS
#define ZpktAPI ZuExport_API
#define ZpktExplicit ZuExport_Explicit
#else
#define ZpktAPI ZuImport_API
#define ZpktExplicit ZuImport_Explicit
#endif
#define ZpktExtern extern ZpktAPI

#else

#define ZpktAPI ZuExport_API
#define ZpktExplicit ZuExport_Explicit
#define ZpktExtern extern ZpktAPI

#endif

namespace Zpkt {

  inline constexpr const char Log[] = "Zpkt";

}

#endif /* ZpktLib_HH */
