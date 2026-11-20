//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z TLS library

#include <openssl/crypto.h>
#include <openssl/err.h>

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

Ztls_Extern const char ZtlsLib[] = "@(#) Z TLS Library v" Z_VERNAME;

namespace Ztls {

  bool cryptoInit()
  {
    static const bool done = OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, nullptr) == 1;
    return done;
  }

  void cryptoClearError() { ERR_clear_error(); }

}
