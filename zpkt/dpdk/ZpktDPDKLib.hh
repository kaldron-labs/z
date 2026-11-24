//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zpkt DPDK plugin exports

#ifndef ZpktDPDKLib_HH
#define ZpktDPDKLib_HH

#include <zlib/ZpktLib.hh>

#ifdef _WIN32

#ifdef ZPKT_DPDK_EXPORTS
#define ZpktDPDKAPI ZuExport_API
#define ZpktDPDKExplicit ZuExport_Explicit
#else
#define ZpktDPDKAPI ZuImport_API
#define ZpktDPDKExplicit ZuImport_Explicit
#endif
#define ZpktDPDKExtern extern ZpktDPDKAPI

#else

#define ZpktDPDKAPI ZuExport_API
#define ZpktDPDKExplicit ZuExport_Explicit
#define ZpktDPDKExtern extern ZpktDPDKAPI

#endif

#endif /* ZpktDPDKLib_HH */
