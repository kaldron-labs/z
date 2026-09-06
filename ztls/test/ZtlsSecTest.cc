//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <string.h>

#include <zlib/ZtlsSec.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsRandom.hh>

using namespace ZuTestUtil;

static void publicKey()
{
  ZuTestScope(publicKey);
  static constexpr ZfCBOR::Limits limits{256, 4, 16, 64};
  enum { Size = 1 + 6 + 2 * (3 + Ztls::ES256::CoordinateSize) };
  uint8_t cose[Size] = {0xa5, 0x01, 0x02, 0x03, 0x26, 0x20, 0x01,
    0x21, 0x58, Ztls::ES256::CoordinateSize};
  unsigned o = 10;
  for (unsigned i = 0; i < Ztls::ES256::CoordinateSize; ++i)
    cose[o++] = uint8_t(i);
  cose[o++] = 0x22;
  cose[o++] = 0x58;
  cose[o++] = Ztls::ES256::CoordinateSize;
  for (unsigned i = 0; i < Ztls::ES256::CoordinateSize; ++i)
    cose[o++] = uint8_t(i + 1);

  uint8_t key[Ztls::ES256::PublicKeySize];
  ZuCheck(Ztls::es256COSEPublicKey(cose, limits, key));
  ZuCheck(key[0] == 4 && key[1] == 0 &&
    key[Ztls::ES256::CoordinateSize] ==
      Ztls::ES256::CoordinateSize - 1 &&
    key[Ztls::ES256::CoordinateSize + 1] == 1);

  char jwk[Ztls::ES256::JWKSize];
  unsigned length;
  ZuCheck(Ztls::es256JWK(key, {jwk, sizeof(jwk)}, length));
  ZuCheck(length == Ztls::ES256::JWKSize);

  cose[4] = 0x27; // alg -8, not ES256
  ZuCheck(!Ztls::es256COSEPublicKey(cose, limits, key));
}

static void verify()
{
  ZuTestScope(verify);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Ztls::PK::SK_EC sk{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t key[Ztls::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(sk.key, key));
  ZuBSpan data{"signed bytes"};
  uint8_t digest[Ztls::MD<>::Size];
  { Ztls::MD<> md; md.update(data); md.finish(digest); }
  uint8_t derSignature[Ztls::ES256::DERMax];
  unsigned signatureLen = 0;
  auto result = sk.sign(rng, digest, [&](ZuBSpan signature) {
    signatureLen = signature.length();
    memcpy(derSignature, signature.data(), signatureLen);
  });
  ZuCheck(!result.template is<ZeException>() && signatureLen &&
    Ztls::es256Verify(key, data, {derSignature, signatureLen}));
  ZuCheck(!Ztls::es256Verify(
    key, ZuBSpan{"changed"}, {derSignature, signatureLen}));
}

int main()
{
  ZuTestMain();
  ZuCheck(Ztls::ctEqual({}, {}));
  ZuCheck(Ztls::ctEqual(ZuBSpan{"secret"}, ZuBSpan{"secret"}));
  ZuCheck(!Ztls::ctEqual(ZuBSpan{"secret"}, ZuBSpan{"secreu"}));
  ZuCheck(!Ztls::ctEqual(ZuBSpan{"secret"}, ZuBSpan{"secret!"}));

  Ztls::Random rng;
  ZuCheck(rng.init());
  uint8_t stored[Ztls::SecretHash::Size];
  ZuCheck(Ztls::secretHash(rng, ZuBSpan{"client secret"}, stored));
  ZuCheck(Ztls::secretVerify(stored, ZuBSpan{"client secret"}));
  ZuCheck(!Ztls::secretVerify(stored, ZuBSpan{"wrong secret"}));
  stored[0]++;
  ZuCheck(!Ztls::secretVerify(stored, ZuBSpan{"client secret"}));
  ZuClear(stored, sizeof(stored));

  uint8_t raw[Ztls::ES256::SignatureSize] = {};
  raw[0] = 0x80;
  raw[Ztls::ES256::CoordinateSize] = 1;
  uint8_t der[Ztls::ES256::DERMax];
  unsigned derLen;
  ZuCheck(Ztls::es256RawToDER(raw, der, derLen));
  uint8_t round[Ztls::ES256::SignatureSize];
  ZuCheck(Ztls::es256DERToRaw({der, derLen}, round));
  ZuCheck(Ztls::ctEqual(raw, round));
  der[derLen] = 0;
  ZuCheck(!Ztls::es256DERToRaw({der, derLen + 1}, round));
  ZuTestCall(publicKey);
  ZuTestCall(verify);
  return 0;
}
