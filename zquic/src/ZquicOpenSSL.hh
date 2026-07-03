//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC OpenSSL internal utilities

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

#include <openssl/evp.h>

#include <zlib/ZmSpecific.hh>

namespace Zquic {

struct OpenSSLCipherCtx {
  OpenSSLCipherCtx() = default;
  ~OpenSSLCipherCtx() { if (ctx) EVP_CIPHER_CTX_free(ctx); }

  EVP_CIPHER_CTX *get() {
    if (!ctx) return ctx = EVP_CIPHER_CTX_new();
    EVP_CIPHER_CTX_reset(ctx);
    return ctx;
  }

  EVP_CIPHER_CTX	*ctx = nullptr;
};

inline EVP_CIPHER_CTX *opensslCipherCtx()
{
  return ZmTLS<OpenSSLCipherCtx>().get();
}

} // namespace Zquic
