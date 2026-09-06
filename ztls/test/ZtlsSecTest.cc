//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <string.h>

#include <zlib/ZtlsSec.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsRandom.hh>

#include <zlib/ZtArray.hh>

using namespace ZuTestUtil;

static void publicKey()
{
  ZuTestScope(publicKey);
  enum { Size = 1 + 6 + 2 * (3 + Ztls::COSE::ES256::CoordinateSize) };
  uint8_t cose[Size] = {0xa5, 0x01, 0x02, 0x03, 0x26, 0x20, 0x01,
    0x21, 0x58, Ztls::COSE::ES256::CoordinateSize};
  static constexpr uint8_t x[Ztls::COSE::ES256::CoordinateSize] = {
    0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47,
    0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40, 0xf2,
    0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0,
    0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96};
  static constexpr uint8_t y[Ztls::COSE::ES256::CoordinateSize] = {
    0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b,
    0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e, 0x16,
    0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce,
    0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5};
  unsigned o = 10;
  memcpy(cose + o, x, sizeof(x));
  o += sizeof(x);
  cose[o++] = 0x22;
  cose[o++] = 0x58;
  cose[o++] = Ztls::COSE::ES256::CoordinateSize;
  memcpy(cose + o, y, sizeof(y));

  uint8_t key[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::COSE::ES256::loadPK(cose, key));
  ZuCheck(key[0] == 4 && key[1] == x[0] &&
    key[Ztls::COSE::ES256::CoordinateSize] == x[Ztls::COSE::ES256::CoordinateSize - 1] &&
    key[Ztls::COSE::ES256::CoordinateSize + 1] == y[0]);

  char jwk[Ztls::COSE::ES256::JWKSize];
  unsigned length;
  ZuCheck(Ztls::COSE::ES256::jwk(key, {jwk, sizeof(jwk)}, length));
  ZuCheck(length == Ztls::COSE::ES256::JWKSize);

  uint8_t unknown[Size + 4];
  memcpy(unknown, cose, sizeof(cose));
  unknown[0] = 0xa6;
  unknown[Size] = 0x18; unknown[Size + 1] = 42;
  unknown[Size + 2] = 0x81; unknown[Size + 3] = 0;
  ZuCheck(Ztls::COSE::ES256::loadPK(unknown, key));

  uint8_t duplicate[Size + 2];
  memcpy(duplicate, cose, sizeof(cose));
  duplicate[0] = 0xa6;
  duplicate[Size] = 0x03; duplicate[Size + 1] = 0x27;
  ZuCheck(!Ztls::COSE::ES256::loadPK(duplicate, key));

  cose[4] = 0x27; // alg -8, not ES256
  ZuCheck(!Ztls::COSE::ES256::loadPK(cose, key));
  auto generic = Ztls::COSE::LoadPK{}.load(cose);
  ZuCheck(!generic.template is<ZeException>());
  cose[4] = 0x26;
  cose[9] = Ztls::COSE::ES256::CoordinateSize - 1;
  ZuCheck(!Ztls::COSE::ES256::loadPK(cose, key));
}

static void verify()
{
  ZuTestScope(verify);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Ztls::PK::SK_EC sk{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t key[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(sk.key, key));
  ZuBSpan data{"signed bytes"};
  uint8_t digest[Ztls::MD<>::Size];
  { Ztls::MD<> md; md.update(data); md.finish(digest); }
  uint8_t derSignature[Ztls::COSE::ES256::DERMax];
  unsigned signatureLen = 0;
  auto result = sk.sign(rng, digest, [&](ZuBSpan signature) {
    signatureLen = signature.length();
    memcpy(derSignature, signature.data(), signatureLen);
  });
  ZuCheck(!result.template is<ZeException>() && signatureLen &&
    Ztls::COSE::ES256::verify(key, data, {derSignature, signatureLen}));
  ZuCheck(!Ztls::COSE::ES256::verify(
    key, ZuBSpan{"changed"}, {derSignature, signatureLen}));
}

template <typename SK>
static bool coseKey(Ztls::Random &rng, SK &sk)
{
  using PK = typename SK::PK;
  ZtBArray encoded;
  auto saved = Ztls::COSE::save(encoded, &sk);
  if (saved.template is<ZeException>()) return false;
  auto loadedSK = Ztls::COSE::LoadSK{rng}.load(encoded);
  if (loadedSK.template is<ZeException>()) return false;
  auto sk_ = dynamic_cast<SK *>(loadedSK.template p<0>().ptr());
  if (!sk_) return false;
  ZtBArray resaved;
  saved = Ztls::COSE::save(resaved, sk_);
  if (saved.template is<ZeException>() || encoded != resaved) return false;
  auto publicResult = sk.mkPK();
  if (publicResult.template is<ZeException>()) return false;
  auto pk = ZuMv(publicResult).template p<0>();
  encoded.length(0);
  saved = Ztls::COSE::save(encoded, pk.ptr());
  if (saved.template is<ZeException>()) return false;
  auto loadedPK = Ztls::COSE::LoadPK{}.load(encoded);
  if (loadedPK.template is<ZeException>()) return false;
  auto pk_ = dynamic_cast<PK *>(loadedPK.template p<0>().ptr());
  if (!pk_) return false;
  resaved.length(0);
  saved = Ztls::COSE::save(resaved, pk_);
  return !saved.template is<ZeException>() && encoded == resaved;
}

static void coseKeys()
{
  ZuTestScope(coseKeys);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Ztls::PK::SK_RSA rsa{rng, 1024};
  ZuCheck(coseKey(rng, rsa));
  Ztls::PK::SK_EC ec{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  ZuCheck(coseKey(rng, ec));
  ZtBArray genericEC;
  uint8_t coordinate[Ztls::COSE::ES256::CoordinateSize] = {};
  ZfCBOR::save(genericEC, Ztls::COSE::Data::EC2PK{
    Ztls::COSE::KeyType::EC2, Ztls::COSE::Curve::P256,
    coordinate, coordinate});
  ZuCheck(genericEC.length() && genericEC[0] == 0xa4);
  Ztls::PK::SK_ED25519 ed25519{rng};
  ZuCheck(coseKey(rng, ed25519));
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

  uint8_t raw[Ztls::COSE::ES256::SignatureSize] = {};
  raw[0] = 0x80;
  raw[Ztls::COSE::ES256::CoordinateSize] = 1;
  uint8_t der[Ztls::COSE::ES256::DERMax];
  unsigned derLen;
  ZuCheck(Ztls::COSE::ES256::rawToDER(raw, der, derLen));
  uint8_t round[Ztls::COSE::ES256::SignatureSize];
  ZuCheck(Ztls::COSE::ES256::derToRaw({der, derLen}, round));
  ZuCheck(Ztls::ctEqual(raw, round));
  der[derLen] = 0;
  ZuCheck(!Ztls::COSE::ES256::derToRaw({der, derLen + 1}, round));
  ZuTestCall(publicKey);
  ZuTestCall(verify);
  ZuTestCall(coseKeys);
  return 0;
}
