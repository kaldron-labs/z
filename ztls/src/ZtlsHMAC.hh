//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// HMAC wrapper (zpicotls/OpenSSL backend)

#ifndef ZtlsHMAC_HH
#define ZtlsHMAC_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZmAssert.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZtlsMD.hh>

namespace Ztls {

template <MDType Type = SHA256> class HMAC {
public:
  enum { Size = MD<Type>::Size };

  HMAC() { }
  ~HMAC() { dispose_(); }

  ZuInline void start(ZuBSpan a) {
    dispose_();
    m_ctx = ptls_hmac_create(
      Backend::hash_algorithm(Type), a.data(), a.length());
    ZmAssert(m_ctx);
  }

  ZuInline void update(ZuBSpan a) {
    ZmAssert(m_ctx);
    m_ctx->update(m_ctx, a.data(), a.length());
  }

  ZuInline void finish(ZuSpan<uint8_t> output) {
    ZmAssert(output.length() >= Size);
    ZmAssert(m_ctx);
    m_ctx->final(m_ctx, &output[0], PTLS_HASH_FINAL_MODE_SNAPSHOT);
  }

  ZuInline void reset() {
    ZmAssert(m_ctx);
    uint8_t tmp[Size];
    m_ctx->final(m_ctx, tmp, PTLS_HASH_FINAL_MODE_RESET);
  }

private:
  void dispose_() {
    if (!m_ctx) return;
    uint8_t tmp[Size];
    m_ctx->final(m_ctx, tmp, PTLS_HASH_FINAL_MODE_FREE);
    m_ctx = nullptr;
  }

  ptls_hash_context_t	*m_ctx = nullptr;
};

}

#endif /* ZtlsHMAC_HH */
