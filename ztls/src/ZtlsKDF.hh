//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// libcrypto key derivation

#ifndef ZtlsKDF_HH
#define ZtlsKDF_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <openssl/types.h>

#include <zlib/ZuSpan.hh>

namespace Ztls::KDF {

class ZtlsAPI HKDF {
public:
  enum { HashSize = 32, MaxSize = 255 * HashSize }; // RFC 5869, SHA256

  HKDF() = default;
  HKDF(const HKDF &) = delete;
  HKDF &operator =(const HKDF &) = delete;
  HKDF(HKDF &&) = delete;
  HKDF &operator =(HKDF &&) = delete;
  ~HKDF();

  bool init();
  bool extract(ZuBSpan salt, ZuBSpan ikm, ZuSpan<uint8_t> prk);
  bool expand(ZuBSpan prk, ZuBSpan info, ZuSpan<uint8_t> output);
  bool derive(ZuBSpan salt, ZuBSpan ikm, ZuBSpan info, ZuSpan<uint8_t> output);

private:
  bool derive_(int, ZuBSpan, ZuBSpan, ZuBSpan, ZuSpan<uint8_t>);
  bool fail(ZuSpan<uint8_t>);

  EVP_KDF_CTX	*m_ctx = nullptr;
};

ZtlsAPI bool scrypt(
  ZuBSpan password, ZuBSpan salt, uint64_t n,
  unsigned r, unsigned p, uint64_t maxMem, ZuSpan<uint8_t> output);

} // Ztls::KDF

#endif /* ZtlsKDF_HH */
