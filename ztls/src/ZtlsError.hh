//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TLS engine diagnostics with a captured libcrypto error

#ifndef ZtlsError_HH
#define ZtlsError_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZePlatform.hh>

namespace Ztls {

  // Owns the operation text and provider error code for deferred exception printing.
  struct Ztls_API Error {
    ZeString		operation;
    unsigned long	provider;
    int			code;

    Error(ZuCSpan operation_, int code_);
    void operator()(ZeLogBuf &s) const;
  };

}

#endif // ZtlsError_HH
