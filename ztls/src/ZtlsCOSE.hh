//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// COSE_Key load/save for the Ztls public/private key types

#ifndef ZtlsCOSE_HH
#define ZtlsCOSE_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZtScratch.hh>

#include <zlib/ZfCBOR.hh>

#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsRandom.hh>

#include <string.h>

namespace Ztls::COSE {

namespace CBOR = ZuFieldProp::CBOR;

ZuDerive(Scratch, (ZtBArray<ZtArrayHeapID<"Ztls.COSE.Scratch",
  ZtArraySharded<true>>>));

namespace ES256 {
  enum {
    CoordinateSize = 32,
    SignatureSize = CoordinateSize * 2,
    // RFC 7518 ES256 is P-256: DER ECDSA has at most two 33-byte INTEGERs.
    DERMax = 72,
    PublicKeySize = 1 + SignatureSize,
    JWKSize = 126
  };

  ZtlsExtern bool rawToDER(ZuBSpan, ZuSpan<uint8_t>, unsigned &length);
  ZtlsExtern bool derToRaw(ZuBSpan, ZuSpan<uint8_t>);
  ZtlsExtern bool verify(ZuBSpan publicKey, ZuBSpan data, ZuBSpan signature);
  ZtlsExtern bool verify(
    ZuBSpan publicKey, ZuBSpan first, ZuBSpan second, ZuBSpan signature);
  ZtlsExtern bool validPK(ZuBSpan);
  ZtlsExtern bool jwk(ZuBSpan, ZuSpan<char>, unsigned &length);
  ZtlsExtern bool loadPK(ZuBSpan, ZuSpan<uint8_t>);
}

namespace Ed25519 {
  enum { KeySize = 32 };
}

namespace RSA {
  // ZtlsPK uses this width for the normal public exponent (RSA_F4).
  enum { ExponentSize = 8 };
}

namespace KeyType {
  enum { OKP = 1, EC2, RSA };
}
namespace Curve {
  enum { P256 = 1, Ed25519 = 6 };
}

namespace Data {

struct Header { uint8_t kty; };
ZfStruct(ZtlsAPI, Header, (kty, (Mutable), UInt8));
ZfStructRender(ZtlsAPI, Header, CBOR, (kty, (CBOR::ID<"1">, CBOR::IntID)));

struct EC2PK { uint8_t kty, crv; ZuBSpan x, y; };
ZfStruct(ZtlsAPI, EC2PK,
  (kty, (Mutable),	UInt8),
  (crv, (Mutable),	UInt8),
  (x, (Mutable),	Bytes),
  (y, (Mutable),	Bytes));
ZfStructRender(ZtlsAPI, EC2PK, CBOR,
  (kty, (CBOR::ID<"1">, CBOR::IntID)),
  (crv, (CBOR::ID<"-1">, CBOR::IntID)),
  (x, (CBOR::ID<"-2">, CBOR::IntID)),
  (y, (CBOR::ID<"-3">, CBOR::IntID)));

struct ES256PK { uint8_t kty; int8_t alg; uint8_t crv; ZuBSpan x, y; };
ZfStruct(ZtlsAPI, ES256PK,
  (kty, (Mutable),	UInt8),
  (alg, (Mutable),	Int8),
  (crv, (Mutable),	UInt8),
  (x, (Mutable),	Bytes),
  (y, (Mutable),	Bytes));
ZfStructRender(ZtlsAPI, ES256PK, CBOR,
  (kty, (CBOR::ID<"1">, CBOR::IntID)),
  (alg, (CBOR::ID<"3">, CBOR::IntID)),
  (crv, (CBOR::ID<"-1">, CBOR::IntID)),
  (x, (CBOR::ID<"-2">, CBOR::IntID)),
  (y, (CBOR::ID<"-3">, CBOR::IntID)));

struct EC2SK { uint8_t kty, crv; ZuBSpan x, y, d; };
ZfStruct(ZtlsAPI, EC2SK,
  (kty, (Mutable),	UInt8),
  (crv, (Mutable),	UInt8),
  (x, (Mutable),	Bytes),
  (y, (Mutable),	Bytes),
  (d, (Mutable),	Bytes));
ZfStructRender(ZtlsAPI, EC2SK, CBOR,
  (kty, (CBOR::ID<"1">, CBOR::IntID)),
  (crv, (CBOR::ID<"-1">, CBOR::IntID)),
  (x, (CBOR::ID<"-2">, CBOR::IntID)),
  (y, (CBOR::ID<"-3">, CBOR::IntID)),
  (d, (CBOR::ID<"-4">, CBOR::IntID)));

struct OKPPK { uint8_t kty, crv; ZuBSpan x; };
ZfStruct(ZtlsAPI, OKPPK,
  (kty, (Mutable),	UInt8),
  (crv, (Mutable),	UInt8),
  (x, (Mutable),	Bytes));
ZfStructRender(ZtlsAPI, OKPPK, CBOR,
  (kty, (CBOR::ID<"1">, CBOR::IntID)),
  (crv, (CBOR::ID<"-1">, CBOR::IntID)),
  (x, (CBOR::ID<"-2">, CBOR::IntID)));

struct OKPSK { uint8_t kty, crv; ZuBSpan x, d; };
ZfStruct(ZtlsAPI, OKPSK,
  (kty, (Mutable),	UInt8),
  (crv, (Mutable),	UInt8),
  (x, (Mutable),	Bytes),
  (d, (Mutable),	Bytes));
ZfStructRender(ZtlsAPI, OKPSK, CBOR,
  (kty, (CBOR::ID<"1">, CBOR::IntID)),
  (crv, (CBOR::ID<"-1">, CBOR::IntID)),
  (x, (CBOR::ID<"-2">, CBOR::IntID)),
  (d, (CBOR::ID<"-4">, CBOR::IntID)));

struct RSAPK { uint8_t kty; ZuBSpan n, e; };
ZfStruct(ZtlsAPI, RSAPK,
  (kty, (Mutable),	UInt8),
  (n, (Mutable),	Bytes),
  (e, (Mutable),	Bytes));
ZfStructRender(ZtlsAPI, RSAPK, CBOR,
  (kty, (CBOR::ID<"1">, CBOR::IntID)),
  (n, (CBOR::ID<"-1">, CBOR::IntID)),
  (e, (CBOR::ID<"-2">, CBOR::IntID)));

struct RSASK { uint8_t kty; ZuBSpan n, e, d, p, q, dp, dq, qi; };
ZfStruct(ZtlsAPI, RSASK,
  (kty, (Mutable),	UInt8),
  (n, (Mutable),	Bytes),
  (e, (Mutable),	Bytes),
  (d, (Mutable),	Bytes),
  (p, (Mutable),	Bytes),
  (q, (Mutable),	Bytes),
  (dp, (Mutable),	Bytes),
  (dq, (Mutable),	Bytes),
  (qi, (Mutable),	Bytes));
ZfStructRender(ZtlsAPI, RSASK, CBOR,
  (kty, (CBOR::ID<"1">, CBOR::IntID)),
  (n, (CBOR::ID<"-1">, CBOR::IntID)),
  (e, (CBOR::ID<"-2">, CBOR::IntID)),
  (d, (CBOR::ID<"-3">, CBOR::IntID)),
  (p, (CBOR::ID<"-4">, CBOR::IntID)),
  (q, (CBOR::ID<"-5">, CBOR::IntID)),
  (dp, (CBOR::ID<"-6">, CBOR::IntID)),
  (dq, (CBOR::ID<"-7">, CBOR::IntID)),
  (qi, (CBOR::ID<"-8">, CBOR::IntID)));

} // Data

inline bool p256(ZuBSpan x, ZuBSpan y, ZuSpan<uint8_t> point)
{
  if (x.length() != ES256::CoordinateSize ||
      y.length() != ES256::CoordinateSize ||
      point.length() < ES256::PublicKeySize) return false;
  point[0] = 4;
  memcpy(point.data() + 1, x.data(), ES256::CoordinateSize);
  memcpy(point.data() + 1 + ES256::CoordinateSize, y.data(),
    ES256::CoordinateSize);
  return true;
}

inline bool p256(const Data::EC2PK &data, ZuSpan<uint8_t> point)
{
  return data.kty == KeyType::EC2 && data.crv == Curve::P256 &&
    p256(data.x, data.y, point);
}

inline bool p256(const Data::ES256PK &data, ZuSpan<uint8_t> point)
{
  return data.kty == KeyType::EC2 && data.crv == Curve::P256 &&
    p256(data.x, data.y, point);
}

inline bool p256(const Data::EC2SK &data, ZuSpan<uint8_t> point)
{
  return data.kty == KeyType::EC2 && data.crv == Curve::P256 &&
    data.d.length() == ES256::CoordinateSize && p256(data.x, data.y, point);
}

class LoadPK {
public:
  ZuUnion<ZmRef<PK::AnyKey>, ZeException> load(ZuBSpan input) const {
    Data::Header header{};
    ZfCBOR::handler<Data::Header>(input).load(header);
    try {
      switch (header.kty) {
        case KeyType::EC2: {
          auto data = ZfCBOR::handler<Data::EC2PK>(input).ctor();
          uint8_t point[ES256::PublicKeySize];
          if (!p256(data, point)) break;
          return ZmRef<PK::AnyKey>{new PK::PK_EC{PK::OIDs::EC_GRP_SECP256R1, point}};
        }
        case KeyType::OKP: {
          auto data = ZfCBOR::handler<Data::OKPPK>(input).ctor();
          if (data.kty != KeyType::OKP || data.crv != Curve::Ed25519 ||
              data.x.length() != Ed25519::KeySize) break;
          return ZmRef<PK::AnyKey>{new PK::PK_ED25519{data.x}};
        }
        case KeyType::RSA: {
          auto data = ZfCBOR::handler<Data::RSAPK>(input).ctor();
          if (data.kty != KeyType::RSA || !data.n || !data.e) break;
          return ZmRef<PK::AnyKey>{new PK::PK_RSA{
            PK::Data::PK_PKCS1{data.n, data.e}}};
        }
      }
    } catch (const ZeException &e) { return e; }
    return ZeEXCEPT(Error, "ZtlsCOSE", "invalid COSE public key");
  }
};

class LoadSK {
public:
  LoadSK(Random &rng) : m_rng(rng) { }

  ZuUnion<ZmRef<PK::AnyKey>, ZeException> load(ZuBSpan input) const {
    Data::Header header{};
    ZfCBOR::handler<Data::Header>(input).load(header);
    try {
      switch (header.kty) {
        case KeyType::EC2: {
          auto data = ZfCBOR::handler<Data::EC2SK>(input).ctor();
          uint8_t point[ES256::PublicKeySize], derived[ES256::PublicKeySize];
          if (!p256(data, point)) break;
          ZmRef<PK::SK_EC> key{new PK::SK_EC{m_rng,
            PK::OIDs::EC_GRP_SECP256R1, data.d}};
          if (!Backend::pkey_ec_export_public(key->key, derived) ||
              memcmp(point, derived, sizeof(point))) break;
          return ZmRef<PK::AnyKey>{ZuMv(key)};
        }
        case KeyType::OKP: {
          auto data = ZfCBOR::handler<Data::OKPSK>(input).ctor();
          if (data.kty != KeyType::OKP || data.crv != Curve::Ed25519 ||
              data.x.length() != Ed25519::KeySize ||
              data.d.length() != Ed25519::KeySize) break;
          ZmRef<PK::SK_ED25519> key{new PK::SK_ED25519{data.d}};
          uint8_t derived[Ed25519::KeySize];
          if (!Backend::pkey_ed25519_export_public(key->key, derived) ||
              memcmp(data.x.data(), derived, sizeof(derived))) break;
          return ZmRef<PK::AnyKey>{ZuMv(key)};
        }
        case KeyType::RSA: {
          auto data = ZfCBOR::handler<Data::RSASK>(input).ctor();
          if (data.kty != KeyType::RSA || !data.n || !data.e || !data.d ||
              !data.p || !data.q || !data.dp || !data.dq || !data.qi) break;
          return ZmRef<PK::AnyKey>{new PK::SK_RSA{PK::Data::SK_PKCS1{
            0, data.n, data.e, data.d, data.p, data.q, data.dp, data.dq,
            data.qi}}};
        }
      }
    } catch (const ZeException &e) { return e; }
    return ZeEXCEPT(Error, "ZtlsCOSE", "invalid COSE private key");
  }

private:
  Random &m_rng;
};

template <typename S>
inline ZuUnion<void, ZeException> save(S &s, const PK::PK_EC *key) {
  unsigned oidLen = Backend::pkey_ec_oid_size(key->key);
  auto oid = ZtScratch(Scratch, oidLen); oid.length(oidLen);
  if (!Backend::pkey_ec_export_oid(key->key, oid) ||
      ZuBSpan{oid} != PK::OIDs::EC_GRP_SECP256R1)
    return ZeEXCEPT(Error, "ZtlsCOSE", "unsupported EC curve");
  auto point = ZtScratch(Scratch, ES256::PublicKeySize);
  point.length(ES256::PublicKeySize);
  if (!Backend::pkey_ec_export_public(key->key, point) || point[0] != 4)
    return ZeEXCEPT(Error, "ZtlsCOSE", "EC public key export failed");
  ZfCBOR::save(s, Data::EC2PK{KeyType::EC2, Curve::P256,
    {point.data() + 1, ES256::CoordinateSize},
    {point.data() + 1 + ES256::CoordinateSize, ES256::CoordinateSize}});
  return {};
}

template <typename S>
inline ZuUnion<void, ZeException> save(S &s, const PK::SK_EC *key) {
  auto point = ZtScratch(Scratch, ES256::PublicKeySize);
  point.length(ES256::PublicKeySize);
  auto d = ZtScratch(Scratch, ES256::CoordinateSize);
  d.length(ES256::CoordinateSize);
  unsigned oidLen = Backend::pkey_ec_oid_size(key->key);
  auto oid = ZtScratch(Scratch, oidLen); oid.length(oidLen);
  if (!Backend::pkey_ec_export_oid(key->key, oid) ||
      ZuBSpan{oid} != PK::OIDs::EC_GRP_SECP256R1)
    return ZeEXCEPT(Error, "ZtlsCOSE", "unsupported EC curve");
  if (!Backend::pkey_ec_export_public(key->key, point) || point[0] != 4 ||
      !Backend::pkey_ec_export_private(key->key, d))
    return ZeEXCEPT(Error, "ZtlsCOSE", "EC private key export failed");
  ZfCBOR::save(s, Data::EC2SK{KeyType::EC2, Curve::P256,
    {point.data() + 1, ES256::CoordinateSize},
    {point.data() + 1 + ES256::CoordinateSize, ES256::CoordinateSize}, d});
  return {};
}

template <typename S>
inline ZuUnion<void, ZeException> save(S &s, const PK::PK_ED25519 *key) {
  uint8_t x[Ed25519::KeySize];
  if (!Backend::pkey_ed25519_export_public(key->key, x))
    return ZeEXCEPT(Error, "ZtlsCOSE", "ED25519 public key export failed");
  ZfCBOR::save(s, Data::OKPPK{KeyType::OKP, Curve::Ed25519, x});
  return {};
}

template <typename S>
inline ZuUnion<void, ZeException> save(S &s, const PK::SK_ED25519 *key) {
  uint8_t x[Ed25519::KeySize], d[Ed25519::KeySize];
  if (!Backend::pkey_ed25519_export_public(key->key, x) ||
      !Backend::pkey_ed25519_export_private(key->key, d))
    return ZeEXCEPT(Error, "ZtlsCOSE", "ED25519 private key export failed");
  ZfCBOR::save(s, Data::OKPSK{KeyType::OKP, Curve::Ed25519, x, d});
  return {};
}

template <typename S>
inline ZuUnion<void, ZeException> save(S &s, const PK::PK_RSA *key) {
  unsigned n = Backend::pkey_rsa_size(key->key);
  auto scratch = ZtScratch(Scratch, n + RSA::ExponentSize);
  ZuSpan<uint8_t> modulus{scratch.data(), n};
  ZuSpan<uint8_t> exponent{scratch.data() + n, RSA::ExponentSize};
  if (!Backend::pkey_rsa_export_public(key->key, modulus, exponent))
    return ZeEXCEPT(Error, "ZtlsCOSE", "RSA public key export failed");
  ZfCBOR::save(s, Data::RSAPK{KeyType::RSA, modulus, exponent});
  return {};
}

template <typename S>
inline ZuUnion<void, ZeException> save(S &s, const PK::SK_RSA *key) {
  unsigned n = Backend::pkey_rsa_size(key->key), p = n >> 1;
  auto scratch = ZtScratch(Scratch, 2 * n + 5 * p + RSA::ExponentSize);
  auto ptr = scratch.data();
  ZuSpan<uint8_t> modulus{ptr, n}; ptr += n;
  ZuSpan<uint8_t> exponent{ptr, RSA::ExponentSize}; ptr += RSA::ExponentSize;
  ZuSpan<uint8_t> d{ptr, n}; ptr += n;
  ZuSpan<uint8_t> prime1{ptr, p}; ptr += p;
  ZuSpan<uint8_t> prime2{ptr, p}; ptr += p;
  ZuSpan<uint8_t> exp1{ptr, p}; ptr += p;
  ZuSpan<uint8_t> exp2{ptr, p}; ptr += p;
  ZuSpan<uint8_t> coeff{ptr, p};
  if (!Backend::pkey_rsa_export_private(key->key, modulus, exponent, d,
      prime1, prime2, exp1, exp2, coeff))
    return ZeEXCEPT(Error, "ZtlsCOSE", "RSA private key export failed");
  ZfCBOR::save(s, Data::RSASK{KeyType::RSA, modulus, exponent, d,
    prime1, prime2, exp1, exp2, coeff});
  return {};
}

inline bool ES256::loadPK(ZuBSpan input, ZuSpan<uint8_t> output)
{
  if (output.length() < ES256::PublicKeySize) return false;
  Data::ES256PK data{};
  ZfCBOR::handler<Data::ES256PK>(input).load(data);
  if (data.alg != -7 || !p256(data, output)) return false;
  try { PK::PK_EC key{PK::OIDs::EC_GRP_SECP256R1,
    {output.data(), ES256::PublicKeySize}}; }
  catch (...) { return false; }
  return true;
}

} // Ztls::COSE

#endif /* ZtlsCOSE_HH */
