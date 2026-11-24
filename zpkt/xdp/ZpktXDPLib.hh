//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z AF_XDP library

#ifndef ZpktXDPLib_HH
#define ZpktXDPLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZPKT_XDP_EXPORTS
#define ZpktXDPAPI ZuExport_API
#define ZpktXDPExplicit ZuExport_Explicit
#else
#define ZpktXDPAPI ZuImport_API
#define ZpktXDPExplicit ZuImport_Explicit
#endif
#define ZpktXDPExtern extern ZpktXDPAPI

#else

#define ZpktXDPAPI ZuExport_API
#define ZpktXDPExplicit ZuExport_Explicit
#define ZpktXDPExtern extern ZpktXDPAPI

#endif

#endif /* ZpktXDPLib_HH */
