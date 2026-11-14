//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtlsMD.hh>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>

#include <zlib/ZePlatform.hh>

namespace Ztls {

MD_::MD_(MDType type)
: m_type{type}, m_size{unsigned(EVP_MD_get_size(evpMD(type)))}
{
  reset();
}

MD_::~MD_() { clear(); }

void MD_::clear()
{
  EVP_MD_CTX_free(m_snapshot);
  EVP_MD_CTX_free(m_ctx);
  m_snapshot = nullptr;
  m_ctx = nullptr;
}

[[noreturn]] void MD_::fail(const char *op, ZuSpan<uint8_t> output)
{
  auto code = ERR_peek_last_error();
  clear();
  if (output.length()) OPENSSL_cleanse(output.data(), output.length());
  throw ZeEXCEPT(Error, "ZtlsMD", ([op, code](auto &s) {
    s << op;
    if (code) {
      char text[256];
      ERR_error_string_n(code, text, sizeof(text));
      s << ": " << text;
    }
  }));
}

void MD_::reset()
{
  if (!cryptoInit()) fail("OPENSSL_init_crypto()");
  if (!m_ctx && !(m_ctx = EVP_MD_CTX_new())) fail("EVP_MD_CTX_new()");
  if (EVP_DigestInit_ex(m_ctx, evpMD(m_type), nullptr) != 1)
    fail("EVP_DigestInit_ex()");
}

void MD_::update(ZuBSpan input)
{
  if (!m_ctx) fail("update() without a valid digest");
  if (EVP_DigestUpdate(m_ctx, input.data(), input.length()) != 1)
    fail("EVP_DigestUpdate()");
}

void MD_::finish(ZuSpan<uint8_t> output)
{
  if (output.length() < m_size) fail("finish() output is too small", output);
  ZuSpan<uint8_t> result{output.data(), m_size};
  if (!m_ctx) fail("finish() without a valid digest", result);
  if (!m_snapshot && !(m_snapshot = EVP_MD_CTX_new()))
    fail("EVP_MD_CTX_new() for snapshot", result);
  if (EVP_MD_CTX_copy_ex(m_snapshot, m_ctx) != 1)
    fail("EVP_MD_CTX_copy_ex()", result);
  unsigned size = 0;
  if (EVP_DigestFinal_ex(m_snapshot, result.data(), &size) != 1 || size != m_size)
    fail("EVP_DigestFinal_ex()", result);
}

bool shake256(ZuBSpan input, ZuSpan<uint8_t> output)
{
  auto ctx = EVP_MD_CTX_new();
  bool ok = ctx && cryptoInit() &&
    EVP_DigestInit_ex(ctx, EVP_shake256(), nullptr) == 1 &&
    EVP_DigestUpdate(ctx, input.data(), input.length()) == 1 &&
    EVP_DigestFinalXOF(ctx, output.data(), output.length()) == 1;
  EVP_MD_CTX_free(ctx);
  if (!ok) ZuClear(output);
  return ok;
}

bool sha3_256(ZuSpan<const ZuBSpan> input, ZuSpan<uint8_t> output)
{
  if (output.length() != 32) return false;
  auto ctx = EVP_MD_CTX_new();
  bool ok = ctx && cryptoInit() &&
    EVP_DigestInit_ex(ctx, EVP_sha3_256(), nullptr) == 1;
  for (unsigned i = 0, n = input.length(); ok && i < n; ++i)
    ok = EVP_DigestUpdate(ctx, input[i].data(), input[i].length()) == 1;
  unsigned size = 0;
  ok = ok && EVP_DigestFinal_ex(ctx, output.data(), &size) == 1 &&
    size == output.length();
  EVP_MD_CTX_free(ctx);
  if (!ok) ZuClear(output);
  return ok;
}

} // Ztls
