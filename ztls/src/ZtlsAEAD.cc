//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtlsAEAD.hh>

#include <limits.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>

namespace Ztls {

ChaCha20Poly1305::~ChaCha20Poly1305() { EVP_CIPHER_CTX_free(m_ctx); }

bool ChaCha20Poly1305::init(bool encrypt, ZuBSpan key)
{
  EVP_CIPHER_CTX_free(m_ctx);
  m_ctx = nullptr;
  m_encrypt = encrypt;
  if (key.length() != KeySize || !cryptoInit()) return false;
#if !defined(OPENSSL_NO_CHACHA) && !defined(OPENSSL_NO_POLY1305)
  m_ctx = EVP_CIPHER_CTX_new();
  if (!m_ctx) return false;
  if (EVP_CipherInit_ex(m_ctx, EVP_chacha20_poly1305(), nullptr,
      key.data(), nullptr, encrypt ? 1 : 0) != 1)
    return fail({});
  return true;
#else
  return false;
#endif
}

bool ChaCha20Poly1305::fail(ZuSpan<uint8_t> output)
{
  if (output.length()) OPENSSL_cleanse(output.data(), output.length());
  EVP_CIPHER_CTX_free(m_ctx);
  m_ctx = nullptr;
  return false;
}

bool ChaCha20Poly1305::aad(ZuBSpan input)
{
  size_t offset = 0, size = input.length();
  while (offset < size) {
    size_t left = size - offset;
    int n = left > INT_MAX ? INT_MAX : int(left);
    int written;
    if (EVP_CipherUpdate(m_ctx, nullptr, &written, input.data() + offset, n) != 1)
      return false;
    offset += n;
  }
  return true;
}

bool ChaCha20Poly1305::data(ZuBSpan input, ZuSpan<uint8_t> output)
{
  size_t offset = 0, size = input.length();
  while (offset < size) {
    size_t left = size - offset;
    int n = left > INT_MAX ? INT_MAX : int(left);
    int written;
    if (EVP_CipherUpdate(m_ctx, output.data() + offset, &written,
        input.data() + offset, n) != 1 || written != n)
      return false;
    offset += n;
  }
  return true;
}

bool ChaCha20Poly1305::seal(
  ZuBSpan nonce, ZuBSpan associated, ZuBSpan input, ZuSpan<uint8_t> output)
{
  size_t n = input.length();
  if (n > SIZE_MAX - TagSize || output.length() < n + TagSize)
    return fail(output);
  ZuSpan<uint8_t> result{output.data(), n + TagSize};
  if (!m_ctx || !m_encrypt || nonce.length() != NonceSize ||
      EVP_CipherInit_ex(m_ctx, nullptr, nullptr, nullptr, nonce.data(), -1) != 1 ||
      !aad(associated) || !data(input, {output.data(), n}))
    return fail(result);
  uint8_t final[EVP_MAX_BLOCK_LENGTH];
  int written = 0;
  bool ok = EVP_CipherFinal_ex(m_ctx, final, &written) == 1 && !written &&
    EVP_CIPHER_CTX_ctrl(m_ctx, EVP_CTRL_AEAD_GET_TAG, TagSize,
      output.data() + n) == 1;
  OPENSSL_cleanse(final, sizeof(final));
  return ok || fail(result);
}

bool ChaCha20Poly1305::open(
  ZuBSpan nonce, ZuBSpan associated, ZuBSpan input, ZuSpan<uint8_t> output)
{
  if (input.length() < TagSize) return fail(output);
  size_t n = input.length() - TagSize;
  if (output.length() < n) return fail(output);
  ZuSpan<uint8_t> result{output.data(), n};
  if (!m_ctx || m_encrypt || nonce.length() != NonceSize ||
      EVP_CipherInit_ex(m_ctx, nullptr, nullptr, nullptr, nonce.data(), -1) != 1 ||
      !aad(associated) || !data({input.data(), n}, result) ||
      EVP_CIPHER_CTX_ctrl(m_ctx, EVP_CTRL_AEAD_SET_TAG, TagSize,
        const_cast<uint8_t *>(input.data() + n)) != 1)
    return fail(result);
  uint8_t final[EVP_MAX_BLOCK_LENGTH];
  int written = 0;
  bool ok = EVP_CipherFinal_ex(m_ctx, final, &written) == 1 && !written;
  OPENSSL_cleanse(final, sizeof(final));
  return ok || fail(result);
}

} // Ztls
