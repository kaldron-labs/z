//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtlsKDF.hh>

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>

#include <zlib/ZmSingleton.hh>

namespace Ztls::KDF {

struct HKDFAlgo {
  EVP_KDF *algo = nullptr;

  HKDFAlgo() {
    if (cryptoInit()) algo = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
  }
  ~HKDFAlgo() { EVP_KDF_free(algo); }
};

using Algo = ZmSingleton<HKDFAlgo, ZmSingletonCleanup<ZmCleanup::Library>>;

HKDF::~HKDF() { EVP_KDF_CTX_free(m_ctx); }

bool HKDF::init()
{
  EVP_KDF_CTX_free(m_ctx);
  m_ctx = nullptr;
  auto algo = Algo::instance()->algo;
  if (!algo) return false;
  m_ctx = EVP_KDF_CTX_new(algo);
  return m_ctx;
}

bool HKDF::fail(ZuSpan<uint8_t> output)
{
  if (output.length()) OPENSSL_cleanse(output.data(), output.length());
  EVP_KDF_CTX_free(m_ctx);
  m_ctx = nullptr;
  return false;
}

bool HKDF::extract(ZuBSpan salt, ZuBSpan ikm, ZuSpan<uint8_t> prk)
{
  if (prk.length() != HashSize) return fail(prk);
  return derive_(EVP_KDF_HKDF_MODE_EXTRACT_ONLY, salt, ikm, {}, prk);
}

bool HKDF::expand(ZuBSpan prk, ZuBSpan info, ZuSpan<uint8_t> output)
{
  return derive_(EVP_KDF_HKDF_MODE_EXPAND_ONLY, {}, prk, info, output);
}

bool HKDF::derive(ZuBSpan salt, ZuBSpan ikm, ZuBSpan info, ZuSpan<uint8_t> output)
{
  return derive_(EVP_KDF_HKDF_MODE_EXTRACT_AND_EXPAND, salt, ikm, info, output);
}

bool HKDF::derive_(
  int mode, ZuBSpan salt, ZuBSpan key, ZuBSpan info, ZuSpan<uint8_t> output)
{
  if (!m_ctx || output.length() > MaxSize) return fail(output);
  if (!output.length()) return true;
  EVP_KDF_CTX_reset(m_ctx);
  uint8_t empty = 0;
  char digest[] = "SHA256";
  OSSL_PARAM params[] = {
    OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode),
    OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
    OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
      salt.length() ? const_cast<uint8_t *>(salt.data()) : &empty, salt.length()),
    OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
      key.length() ? const_cast<uint8_t *>(key.data()) : &empty, key.length()),
    OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
      info.length() ? const_cast<uint8_t *>(info.data()) : &empty, info.length()),
    OSSL_PARAM_construct_end()
  };
  if (EVP_KDF_derive(m_ctx, output.data(), output.length(), params) != 1)
    return fail(output);
  return true;
}

bool scrypt(
  ZuBSpan password, ZuBSpan salt, uint64_t n,
  unsigned r, unsigned p, uint64_t maxMem, ZuSpan<uint8_t> output)
{
  bool ok = cryptoInit() && EVP_PBE_scrypt(
    ZuCSpan{password}.data(), password.length(),
    salt.data(), salt.length(), n, r, p, maxMem,
    output.data(), output.length()) == 1;
  if (!ok && output.length()) OPENSSL_cleanse(output.data(), output.length());
  return ok;
}

} // Ztls::KDF
