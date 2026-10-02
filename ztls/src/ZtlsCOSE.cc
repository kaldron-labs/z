//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtlsCOSE.hh>

#include <openssl/ecdsa.h>

#include <string.h>

#include <zlib/ZtlsMD.hh>

namespace Ztls::COSE::ES256 {

bool rawToDER(ZuBSpan raw, ZuSpan<uint8_t> der, unsigned &length)
{
  length = 0;
  if (raw.length() != SignatureSize) return false;
  BIGNUM *r = BN_bin2bn(raw.data(), CoordinateSize, nullptr);
  BIGNUM *s = BN_bin2bn(raw.data() + CoordinateSize, CoordinateSize, nullptr);
  ECDSA_SIG *sig = ECDSA_SIG_new();
  if (!r || !s || !sig || ECDSA_SIG_set0(sig, r, s) != 1) {
    BN_free(r); BN_free(s); ECDSA_SIG_free(sig);
    return false;
  }
  int n = i2d_ECDSA_SIG(sig, nullptr);
  if (n <= 0 || unsigned(n) > der.length()) {
    ECDSA_SIG_free(sig);
    return false;
  }
  unsigned char *p = der.data();
  bool ok = i2d_ECDSA_SIG(sig, &p) == n;
  ECDSA_SIG_free(sig);
  if (!ok) return false;
  length = unsigned(n);
  return true;
}

bool derToRaw(ZuBSpan der, ZuSpan<uint8_t> raw)
{
  if (!der || raw.length() < SignatureSize) return false;
  const unsigned char *p = der.data();
  ECDSA_SIG *sig = d2i_ECDSA_SIG(nullptr, &p, der.length());
  if (!sig || p != der.end()) { ECDSA_SIG_free(sig); return false; }
  const BIGNUM *r, *s;
  ECDSA_SIG_get0(sig, &r, &s);
  bool ok = !BN_is_negative(r) && !BN_is_negative(s) &&
    BN_num_bytes(r) <= CoordinateSize && BN_num_bytes(s) <= CoordinateSize &&
    BN_bn2binpad(r, raw.data(), CoordinateSize) == CoordinateSize &&
    BN_bn2binpad(s, raw.data() + CoordinateSize, CoordinateSize) == CoordinateSize;
  ECDSA_SIG_free(sig);
  if (!ok) ZuClear(raw.data(), SignatureSize);
  return ok;
}

static bool verify_(ZuBSpan publicKey, ZuBSpan digest, ZuBSpan signature)
{
  if (publicKey.length() != PublicKeySize || publicKey[0] != 4 ||
      digest.length() != MD<>::Size || !signature) return false;
  try {
    PK::PK_EC key{PK::OIDs::EC_GRP_SECP256R1, publicKey};
    auto result = key.verify(digest, signature);
    return !result.template is<ZeException>() && result.template p<0>();
  } catch (...) { return false; }
}

bool verify(ZuBSpan publicKey, ZuBSpan data, ZuBSpan signature)
{
  if (!data) return false;
  uint8_t digest[MD<>::Size];
  { MD<> md; md.update(data); md.finish(digest); }
  return verify_(publicKey, digest, signature);
}

bool verify(ZuBSpan publicKey, ZuBSpan first, ZuBSpan second, ZuBSpan signature)
{
  if (!first || !second) return false;
  uint8_t digest[MD<>::Size];
  { MD<> md; md.update(first); md.update(second); md.finish(digest); }
  return verify_(publicKey, digest, signature);
}

bool validPK(ZuBSpan publicKey)
{
  if (publicKey.length() != PublicKeySize || publicKey[0] != 4) return false;
  try { PK::PK_EC key{PK::OIDs::EC_GRP_SECP256R1, publicKey}; }
  catch (...) { return false; }
  return true;
}

bool jwk(ZuBSpan key, ZuSpan<char> out, unsigned &length)
{
  static constexpr auto prefix = "{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\""_z;
  static constexpr auto middle = "\",\"y\":\""_z;
  static constexpr auto suffix = "\"}"_z;
  length = 0;
  if (key.length() != PublicKeySize || key[0] != 4 || out.length() < JWKSize)
    return false;
  char *p = out.data();
  memcpy(p, prefix.data(), prefix.length()); p += prefix.length();
  p += ZuBase64URL::encode({reinterpret_cast<uint8_t *>(p), unsigned(out.end() - p)},
    {key.data() + 1, CoordinateSize});
  memcpy(p, middle.data(), middle.length()); p += middle.length();
  p += ZuBase64URL::encode({reinterpret_cast<uint8_t *>(p), unsigned(out.end() - p)},
    {key.data() + 1 + CoordinateSize, CoordinateSize});
  memcpy(p, suffix.data(), suffix.length()); p += suffix.length();
  length = unsigned(p - out.data());
  return length == JWKSize;
}

} // Ztls::COSE::ES256
