//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// mbedtls C++ wrapper - message digest

#ifndef ZtlsMD_HH
#define ZtlsMD_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <mbedtls/md.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>

namespace Ztls {

template <mbedtls_md_type_t = MBEDTLS_MD_SHA256> class MD;

template <> class MD<MBEDTLS_MD_SHA1> {
public:
  enum { Size = 20 };

  MD() {
    mbedtls_sha1_init(&m_ctx);
    reset();
  }
  ~MD() {
    mbedtls_sha1_free(&m_ctx);
  }

  ZuInline void update(ZuBSpan a) {
    mbedtls_sha1_update(&m_ctx, a.data(), a.length());
  }

  ZuInline void finish(ZuSpan<uint8_t> output) {
    ZmAssert(output.length() >= Size);
    mbedtls_sha1_finish(&m_ctx, &output[0]);
  }

  ZuInline void reset() {
    mbedtls_sha1_starts(&m_ctx);
  }

private:
  mbedtls_sha1_context	m_ctx;
};

template <> class MD<MBEDTLS_MD_SHA256> {
public:
  enum { Size = 32 };

  MD() {
    mbedtls_sha256_init(&m_ctx);
    reset();
  }
  ~MD() {
    mbedtls_sha256_free(&m_ctx);
  }

  ZuInline void update(ZuBSpan a) {
    mbedtls_sha256_update(&m_ctx, a.data(), a.length());
  }

  ZuInline void finish(ZuSpan<uint8_t> output) {
    ZmAssert(output.length() >= Size);
    mbedtls_sha256_finish(&m_ctx, &output[0]);
  }

  ZuInline void reset() {
    mbedtls_sha256_starts(&m_ctx, 0);
  }

private:
  mbedtls_sha256_context	m_ctx;
};

template <> class MD<MBEDTLS_MD_SHA384> {
public:
  enum { Size = 48 };

  MD() {
    mbedtls_sha512_init(&m_ctx);
    reset();
  }
  ~MD() {
    mbedtls_sha512_free(&m_ctx);
  }

  ZuInline void update(ZuBSpan a) {
    mbedtls_sha512_update(&m_ctx, a.data(), a.length());
  }

  ZuInline void finish(ZuSpan<uint8_t> output) {
    ZmAssert(output.length() >= Size);
    mbedtls_sha512_finish(&m_ctx, &output[0]);
  }

  ZuInline void reset() {
    mbedtls_sha512_starts(&m_ctx, 1);
  }

private:
  mbedtls_sha512_context	m_ctx;
};

template <> class MD<MBEDTLS_MD_SHA512> {
public:
  enum { Size = 64 };

  MD() {
    mbedtls_sha512_init(&m_ctx);
    reset();
  }
  ~MD() {
    mbedtls_sha512_free(&m_ctx);
  }

  ZuInline void update(ZuBSpan a) {
    mbedtls_sha512_update(&m_ctx, a.data(), a.length());
  }

  ZuInline void finish(ZuSpan<uint8_t> output) {
    ZmAssert(output.length() >= Size);
    mbedtls_sha512_finish(&m_ctx, &output[0]);
  }

  ZuInline void reset() {
    mbedtls_sha512_starts(&m_ctx, 0);
  }

private:
  mbedtls_sha512_context	m_ctx;
};

}

#endif /* ZtlsMD_HH */
