//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z TLS library

#ifndef ZtlsLib_HH
#define ZtlsLib_HH

#include <zlib/ZuLib.hh>

#ifndef ZtlsExports
#define ZtlsExports 0
#endif

#ifndef ZtlsUseBrotli
#define ZtlsUseBrotli 0
#endif

#ifdef _WIN32

#if ZtlsExports
#define Ztls_API ZuExport_API
#define Ztls_Explicit ZuExport_Explicit
#else
#define Ztls_API ZuImport_API
#define Ztls_Explicit ZuImport_Explicit
#endif
#define Ztls_Extern extern Ztls_API

#else

#define Ztls_API ZuExport_API
#define Ztls_Explicit ZuExport_Explicit
#define Ztls_Extern extern Ztls_API

#endif

namespace Ztls {

  Ztls_API bool cryptoInit();		// optional
  Ztls_API void cryptoClearError();

}

// ZtlsLib_HH
#endif
