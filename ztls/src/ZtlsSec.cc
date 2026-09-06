//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// security-sensitive byte comparisons

#include <zlib/ZtlsSec.hh>

#include <openssl/evp.h>
#include <openssl/ecdsa.h>

#include <string.h>

#include <zlib/ZuBase64URL.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsRandom.hh>

namespace Ztls {

bool ctEqual(ZuBSpan l, ZuBSpan r)
{
  unsigned n = l.length();
  if (n != r.length()) return false;
  uint8_t v = 0;
  for (unsigned i = 0; i < n; ++i) v |= l[i] ^ r[i];
  return !v;
}

static bool secretHash_(ZuBSpan secret, ZuBSpan salt, ZuSpan<uint8_t> digest)
{
  // RFC 7914 scrypt; this fixed profile uses about 32MiB per operation.
  enum { R = 8, P = 1 };
  constexpr uint64_t N = 1U<<15;
  constexpr uint64_t MaxMem = 64U<<20;
  return EVP_PBE_scrypt(
    reinterpret_cast<const char *>(secret.data()), secret.length(),
    salt.data(), salt.length(), N, R, P, MaxMem,
    digest.data(), SecretHash::DigestSize) == 1;
}

bool secretHash(Random &rng, ZuBSpan secret, ZuSpan<uint8_t> output)
{
  if (output.length() < SecretHash::Size) return false;
  output[0] = SecretHash::Version;
  ZuSpan<uint8_t> salt{output.data() + 1, SecretHash::SaltSize};
  ZuSpan<uint8_t> digest{
    output.data() + 1 + SecretHash::SaltSize, SecretHash::DigestSize};
  if (rng.random(salt) && secretHash_(secret, salt, digest)) return true;
  ZuClear(output.data(), SecretHash::Size);
  return false;
}

bool secretVerify(ZuBSpan stored, ZuBSpan secret)
{
  if (stored.length() != SecretHash::Size ||
      stored[0] != SecretHash::Version) return false;
  ZuBSpan salt{stored.data() + 1, SecretHash::SaltSize};
  ZuBSpan digest{
    stored.data() + 1 + SecretHash::SaltSize, SecretHash::DigestSize};
  uint8_t candidate[SecretHash::DigestSize];
  bool ok = secretHash_(secret, salt, candidate) &&
    ctEqual(candidate, digest);
  ZuClear(candidate, sizeof(candidate));
  return ok;
}

bool es256RawToDER(
    ZuBSpan raw, ZuSpan<uint8_t> der, unsigned &length)
{
  length = 0;
  if (raw.length() != ES256::SignatureSize) return false;
  BIGNUM *r = BN_bin2bn(raw.data(), ES256::CoordinateSize, nullptr);
  BIGNUM *s = BN_bin2bn(
    raw.data() + ES256::CoordinateSize, ES256::CoordinateSize, nullptr);
  ECDSA_SIG *sig = ECDSA_SIG_new();
  if (!r || !s || !sig || ECDSA_SIG_set0(sig, r, s) != 1) {
    BN_free(r);
    BN_free(s);
    ECDSA_SIG_free(sig);
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

bool es256DERToRaw(ZuBSpan der, ZuSpan<uint8_t> raw)
{
  if (!der || raw.length() < ES256::SignatureSize) return false;
  const unsigned char *p = der.data();
  ECDSA_SIG *sig = d2i_ECDSA_SIG(nullptr, &p, der.length());
  if (!sig || p != der.end()) {
    ECDSA_SIG_free(sig);
    return false;
  }
  const BIGNUM *r, *s;
  ECDSA_SIG_get0(sig, &r, &s);
  bool ok = !BN_is_negative(r) && !BN_is_negative(s) &&
    BN_num_bytes(r) <= ES256::CoordinateSize &&
    BN_num_bytes(s) <= ES256::CoordinateSize &&
    BN_bn2binpad(r, raw.data(), ES256::CoordinateSize) ==
      ES256::CoordinateSize &&
    BN_bn2binpad(s, raw.data() + ES256::CoordinateSize,
      ES256::CoordinateSize) == ES256::CoordinateSize;
  ECDSA_SIG_free(sig);
  if (!ok) ZuClear(raw.data(), ES256::SignatureSize);
  return ok;
}

static bool es256Verify_(
    ZuBSpan publicKey, ZuBSpan digest, ZuBSpan signature)
{
  if (publicKey.length() != ES256::PublicKeySize || publicKey[0] != 4 ||
      digest.length() != MD<>::Size || !signature) return false;
  try {
    PK::PK_EC key{PK::OIDs::EC_GRP_SECP256R1, publicKey};
    auto result = key.verify(digest, signature);
    return !result.template is<ZeException>() && result.template p<0>();
  } catch (...) {
    return false;
  }
}

bool es256Verify(ZuBSpan publicKey, ZuBSpan data, ZuBSpan signature)
{
  if (!data) return false;
  uint8_t digest[MD<>::Size];
  { MD<> md; md.update(data); md.finish(digest); }
  return es256Verify_(publicKey, digest, signature);
}

bool es256Verify(
    ZuBSpan publicKey, ZuBSpan first, ZuBSpan second, ZuBSpan signature)
{
  if (!first || !second) return false;
  uint8_t digest[MD<>::Size];
  {
    MD<> md;
    md.update(first);
    md.update(second);
    md.finish(digest);
  }
  return es256Verify_(publicKey, digest, signature);
}

bool es256PublicKeyValid(ZuBSpan publicKey)
{
  if (publicKey.length() != ES256::PublicKeySize || publicKey[0] != 4)
    return false;
  try {
    PK::PK_EC key{PK::OIDs::EC_GRP_SECP256R1, publicKey};
    return true;
  } catch (...) {
    return false;
  }
}

struct COSEContext {
  ZuSpan<uint8_t>	out;
  int64_t		label = 0;
  uint8_t		seen = 0;
  bool			key = true;
};

static bool coseVisit(void *ptr, const ZfCBOR::Item &item)
{
  auto &ctx = *static_cast<COSEContext *>(ptr);
  if (!item.depth)
    return item.type == ZfCBOR::Type::Map && ctx.key;
  if (item.depth != 1) return true;
  if (ctx.key) {
    if (item.type == ZfCBOR::Type::UInt) {
      if (item.value > INT64_MAX) return false;
      ctx.label = int64_t(item.value);
    } else if (item.type == ZfCBOR::Type::NInt) {
      if (item.value > INT64_MAX) return false;
      ctx.label = -1 - int64_t(item.value);
    } else {
      return false;
    }
    ctx.key = false;
    return true;
  }

  ctx.key = true;
  unsigned bit;
  switch (ctx.label) {
    case 1:
      bit = 1U<<0;
      if (item.type != ZfCBOR::Type::UInt || item.value != 2) return false;
      break;
    case 3:
      bit = 1U<<1;
      if (item.type != ZfCBOR::Type::NInt || item.value != 6) return false;
      break;
    case -1:
      bit = 1U<<2;
      if (item.type != ZfCBOR::Type::UInt || item.value != 1) return false;
      break;
    case -2:
      bit = 1U<<3;
      if (item.type != ZfCBOR::Type::Bytes ||
	  item.data.length() != ES256::CoordinateSize) return false;
      memcpy(ctx.out.data() + 1, item.data.data(), ES256::CoordinateSize);
      break;
    case -3:
      bit = 1U<<4;
      if (item.type != ZfCBOR::Type::Bytes ||
	  item.data.length() != ES256::CoordinateSize) return false;
      memcpy(ctx.out.data() + 1 + ES256::CoordinateSize,
	item.data.data(), ES256::CoordinateSize);
      break;
    default: return true;
  }
  if (ctx.seen & bit) return false;
  ctx.seen |= bit;
  return true;
}

bool es256COSEPublicKey(
    ZuBSpan cose, const ZfCBOR::Limits &limits, ZuSpan<uint8_t> out)
{
  if (out.length() < ES256::PublicKeySize) return false;
  COSEContext ctx{out};
  auto result = ZfCBOR::scan(cose, limits, &ctx, coseVisit);
  if (!result || !ctx.key || ctx.seen != 0x1f) return false;
  out[0] = 4;
  return true;
}

bool es256JWK(ZuBSpan key, ZuSpan<char> out, unsigned &length)
{
  static constexpr ZuCSpan prefix = "{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"";
  static constexpr ZuCSpan middle = "\",\"y\":\"";
  static constexpr ZuCSpan suffix = "\"}";
  length = 0;
  if (key.length() != ES256::PublicKeySize || key[0] != 4 ||
      out.length() < ES256::JWKSize) return false;
  char *p = out.data();
  memcpy(p, prefix.data(), prefix.length()); p += prefix.length();
  p += ZuBase64URL::encode(
    {reinterpret_cast<uint8_t *>(p), unsigned(out.end() - p)},
    {key.data() + 1, ES256::CoordinateSize});
  memcpy(p, middle.data(), middle.length()); p += middle.length();
  p += ZuBase64URL::encode(
    {reinterpret_cast<uint8_t *>(p), unsigned(out.end() - p)},
    {key.data() + 1 + ES256::CoordinateSize, ES256::CoordinateSize});
  memcpy(p, suffix.data(), suffix.length()); p += suffix.length();
  length = unsigned(p - out.data());
  return length == ES256::JWKSize;
}

}
