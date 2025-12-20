//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// mbedtls C++ wrapper - random number generator

#ifndef ZtlsRandom_HH
#define ZtlsRandom_HH

#include <zlib/ZtlsLib.hh>

#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

namespace Ztls {

class Random {
public:
  Random() {
    mbedtls_entropy_init(&m_entropy);
    mbedtls_ctr_drbg_init(&m_ctr_drbg);
  }
  ~Random() {
    mbedtls_ctr_drbg_free(&m_ctr_drbg);
    mbedtls_entropy_free(&m_entropy);
  }

  bool init() {
    int n = mbedtls_ctr_drbg_seed(
	&m_ctr_drbg, mbedtls_entropy_func, &m_entropy, 0, 0);
    return !n;
  }

  bool random(ZuSpan<uint8_t> data) {
    int i = mbedtls_ctr_drbg_random(&m_ctr_drbg, data.data(), data.length());
    return i >= 0;
  }

  ZuInline mbedtls_ctr_drbg_context *ctr_drbg() { return &m_ctr_drbg; }

private:
  mbedtls_entropy_context	m_entropy;
  mbedtls_ctr_drbg_context	m_ctr_drbg;
};

}

#endif /* ZtlsRandom_HH */
