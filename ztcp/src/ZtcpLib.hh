//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Raw TCP/IP transport

#ifndef ZtcpLib_HH
#define ZtcpLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZTCP_EXPORTS
#define ZtcpAPI ZuExport_API
#define ZtcpExplicit ZuExport_Explicit
#else
#define ZtcpAPI ZuImport_API
#define ZtcpExplicit ZuImport_Explicit
#endif
#define ZtcpExtern extern ZtcpAPI

#else

#define ZtcpAPI ZuExport_API
#define ZtcpExplicit ZuExport_Explicit
#define ZtcpExtern extern ZtcpAPI

#endif

namespace Ztcp {

ZtcpExtern void lib_init(); // can be called repeatedly

}

#endif /* ZtcpLib_HH */
