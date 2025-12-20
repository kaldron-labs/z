//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z I/O library main header

#ifndef ZiLib_HH
#define ZiLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZI_EXPORTS
#define ZiAPI ZuExport_API
#define ZiExplicit ZuExport_Explicit
#else
#define ZiAPI ZuImport_API
#define ZiExplicit ZuImport_Explicit
#endif
#define ZiExtern extern ZiAPI

#else

#define ZiAPI
#define ZiExplicit
#define ZiExtern extern

#endif

#include <zlib/ZuIOResult.hh>

namespace Zi {
  using namespace Zu::IO;
};

#ifndef _WIN32
#define ZiENOMEM ENOMEM
#define ZiENOENT ENOENT
#define ZiEINVAL EINVAL
#define ZiENOTCONN ENOTCONN
#define ZiECONNRESET ECONNRESET
#define ZiENOBUFS ENOBUFS
#define ZiEADDRINUSE EADDRINUSE
#else
#define ZiENOMEM ERROR_NOT_ENOUGH_MEMORY
#define ZiENOENT ERROR_FILE_NOT_FOUND
#define ZiEINVAL WSAEINVAL
#define ZiENOTCONN WSAEDISCON
#define ZiECONNRESET WSAECONNRESET
#define ZiENOBUFS WSAENOBUFS
#define ZiEADDRINUSE WSAEADDRINUSE
#endif

#endif /* ZiLib_HH */
