//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// mbedtls C++ wrapper - HMAC message digest

#ifndef ZtlsHMAC_HH
#define ZtlsHMAC_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZtlsMD.hh>

namespace Ztls {

// we do not compile-time dispatch to mbedtls_sha* here due to
// mbedtls HMAC API limitations

template <mbedtls_md_type_t Type = MBEDTLS_MD_SHA256> class HMAC {
public:
  enum { Size = MD<Type>::Size };

  HMAC() {
    mbedtls_md_init(&m_ctx);
    mbedtls_md_setup(&m_ctx,
      mbedtls_md_info_from_type(mbedtls_md_type_t(Type)), 1);
  }
  ~HMAC() {
    mbedtls_md_free(&m_ctx);
  }

  ZuInline void start(ZuBSpan a) {
    mbedtls_md_hmac_starts(&m_ctx, a.data(), a.length());
  }

  ZuInline void update(ZuBSpan a) {
    mbedtls_md_hmac_update(&m_ctx, a.data(), a.length());
  }

  ZuInline void finish(ZuSpan<uint8_t> output) {
    ZmAssert(output.length() >= Size);
    mbedtls_md_hmac_finish(&m_ctx, &output[0]);
  }

  ZuInline void reset() {
    mbedtls_md_hmac_reset(&m_ctx);
  }

private:
  mbedtls_md_context_t	m_ctx;
};

}

#endif /* ZtlsHMAC_HH */
