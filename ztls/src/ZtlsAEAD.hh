//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// libcrypto ChaCha20-Poly1305 authenticated encryption

#ifndef ZtlsAEAD_HH
#define ZtlsAEAD_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <openssl/types.h>

#include <zlib/ZuSpan.hh>

namespace Ztls {

class ZtlsAPI ChaCha20Poly1305 {
public:
  // RFC 8439 AEAD key, nonce, and authenticator lengths.
  enum { KeySize = 32, NonceSize = 12, TagSize = 16 };

  ChaCha20Poly1305() = default;
  ChaCha20Poly1305(const ChaCha20Poly1305 &) = delete;
  ChaCha20Poly1305 &operator =(const ChaCha20Poly1305 &) = delete;
  ChaCha20Poly1305(ChaCha20Poly1305 &&) = delete;
  ChaCha20Poly1305 &operator =(ChaCha20Poly1305 &&) = delete;
  ~ChaCha20Poly1305();

  bool init(bool encrypt, ZuBSpan key);
  bool seal(ZuBSpan nonce, ZuBSpan aad, ZuBSpan input, ZuSpan<uint8_t> output);
  bool open(ZuBSpan nonce, ZuBSpan aad, ZuBSpan input, ZuSpan<uint8_t> output);

private:
  bool fail(ZuSpan<uint8_t>);
  bool aad(ZuBSpan);
  bool data(ZuBSpan, ZuSpan<uint8_t>);

  EVP_CIPHER_CTX	*m_ctx = nullptr;
  bool		m_encrypt = false;
};

} // Ztls

#endif /* ZtlsAEAD_HH */
