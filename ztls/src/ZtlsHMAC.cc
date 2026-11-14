//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtlsHMAC.hh>

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include <zlib/ZmSingleton.hh>
#include <zlib/ZePlatform.hh>

namespace Ztls {

struct MACAlgo {
  EVP_MAC *mac = cryptoInit() ? EVP_MAC_fetch(nullptr, "HMAC", nullptr) : nullptr;

  ~MACAlgo() { EVP_MAC_free(mac); }
};

HMAC_::HMAC_(MDType type)
: m_type{type}, m_size{unsigned(EVP_MD_get_size(evpMD(type)))} { }

HMAC_::~HMAC_() { clear(); }

void HMAC_::clear()
{
  EVP_MAC_CTX_free(m_ctx);
  m_ctx = nullptr;
}

[[noreturn]] void HMAC_::fail(const char *op, ZuSpan<uint8_t> output)
{
  auto code = ERR_peek_last_error();
  clear();
  if (output.length()) OPENSSL_cleanse(output.data(), output.length());
  throw ZeEXCEPT(Error, "ZtlsHMAC", ([op, code](auto &s) {
    s << op;
    if (code) {
      char text[256];
      ERR_error_string_n(code, text, sizeof(text));
      s << ": " << text;
    }
  }));
}

void HMAC_::start(ZuBSpan key)
{
  clear();
  auto algo = ZmSingleton<MACAlgo,
    ZmSingletonCleanup<ZmCleanup::Library>>::instance()->mac;
  if (!algo) fail("EVP_MAC_fetch(HMAC)");
  if (!(m_ctx = EVP_MAC_CTX_new(algo))) fail("EVP_MAC_CTX_new()");
  OSSL_PARAM params[] = {
    OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,
      const_cast<char *>(EVP_MD_get0_name(evpMD(m_type))), 0),
    OSSL_PARAM_construct_end()
  };
  uint8_t empty = 0;
  if (EVP_MAC_init(m_ctx, key.length() ? key.data() : &empty,
      key.length(), params) != 1)
    fail("EVP_MAC_init()");
}

void HMAC_::update(ZuBSpan input)
{
  if (!m_ctx) fail("update() without a keyed MAC");
  if (EVP_MAC_update(m_ctx, input.data(), input.length()) != 1)
    fail("EVP_MAC_update()");
}

void HMAC_::finish(ZuSpan<uint8_t> output)
{
  if (output.length() < m_size) fail("finish() output is too small", output);
  ZuSpan<uint8_t> result{output.data(), m_size};
  if (!m_ctx) fail("finish() without a keyed MAC", result);
  size_t size = 0;
  if (EVP_MAC_final(m_ctx, result.data(), &size, m_size) != 1 || size != m_size)
    fail("EVP_MAC_final()", result);
  if (EVP_MAC_init(m_ctx, nullptr, 0, nullptr) != 1)
    fail("EVP_MAC_init() after finish", result);
}

void HMAC_::reset()
{
  if (!m_ctx) fail("reset() without a keyed MAC");
  if (EVP_MAC_init(m_ctx, nullptr, 0, nullptr) != 1)
    fail("EVP_MAC_init() for reset");
}

} // Ztls
