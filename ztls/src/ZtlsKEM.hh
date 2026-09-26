//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OpenSSL-backed ML-KEM-768 key encapsulation and ASN.1 key serialization

#ifndef ZtlsKEM_HH
#define ZtlsKEM_HH

#ifndef ZtlsLib_HH
#include <zlib/ZuDerive.hh>
#include <zlib/ZtlsLib.hh>
#endif

#include <string.h>

#include <zlib/ZmVHeap.hh>

#include <zlib/ZtlsPK.hh>

namespace Ztls::PK {

using KEMScratchHeap = ZmVHeap<"Ztls.KEM.Scratch">;

enum {
  MLKEM768PublicSize = 1184,
  MLKEM768SeedSize = 64,
  MLKEM768PrivateSize = 2400,
  MLKEM768CiphertextSize = 1088,
  MLKEM768SecretSize = 32
};

template <typename Heap = ZuVoid>
struct PK_MLKEM768_ : public Heap, public AnyPK {
  enum { Secret = 0 };

protected:
  PK_MLKEM768_() { }

public:
  PK_MLKEM768_(ZuBSpan pubKey) {
    if (!Backend::pkey_mlkem768_import_public(key, pubKey))
      throw ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 public key import failed");
  }

  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const {
    auto pubKey = ZmScratch(
      uint8_t, MLKEM768PublicSize, KEMScratchHeap);
    pubKey.length(MLKEM768PublicSize);
    if (!Backend::pkey_mlkem768_export_public(key, pubKey))
      return ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 public key export failed");
    Data::PK_X509_MLKEM data{.id = OIDs::MLKEM768, .pubKey = pubKey};
    ZfASN1::save(s, data);
    return {};
  }

  ZuUnion<void, ZeException> exportPK(ZuSpan<uint8_t> pubKey) const {
    if (!Backend::pkey_mlkem768_export_public(key, pubKey))
      return ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 public key export failed");
    return {};
  }

  ZuUnion<void, ZeException>
  encapsulate(
    Random &rng, ZuSpan<uint8_t> ciphertext, ZuSpan<uint8_t> secret) const
  {
    (void)rng;
    if (!Backend::pkey_mlkem768_encapsulate(key, ciphertext, secret))
      return ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 encapsulation failed");
    return {};
  }
};
ZuDerive(PK_MLKEM768_Heap, (ZmHeap<"Ztls.PK_MLKEM768", PK_MLKEM768_<>>));
ZuDerive(PK_MLKEM768, (PK_MLKEM768_<PK_MLKEM768_Heap>));

template <typename Heap = ZuVoid>
struct SK_MLKEM768_ : public PK_MLKEM768_<Heap> {
  enum { Secret = 1 };
  using PK = PK_MLKEM768;
  using PK_ = PK_MLKEM768_<Heap>;
  using PK_::key;

  SK_MLKEM768_(Random &rng) {
    (void)rng;
    if (!Backend::pkey_mlkem768_generate(key))
      throw ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 key generation failed");
  }

  SK_MLKEM768_(ZuBSpan bytes) {
    bool ok = bytes.length() == MLKEM768SeedSize ?
      Backend::pkey_mlkem768_import_seed(key, bytes) :
      Backend::pkey_mlkem768_import_private(key, bytes);
    if (!ok)
      throw ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 private key import failed");
  }

  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const {
    auto bytes = ZmScratch(
      uint8_t, MLKEM768PrivateSize, KEMScratchHeap);
    bytes.length(MLKEM768PrivateSize);
    size_t n = 0;
    if (!Backend::pkey_mlkem768_export_private(key, bytes, &n)) {
      ZuClear(bytes);
      return ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 private key export failed");
    }
    if (n == MLKEM768SeedSize) {
      Data::SK_PKCS8_MLKEM_SEED data{
        .version = 0, .id = OIDs::MLKEM768,
        .seed = ZuBSpan{bytes.data(), n}
      };
      ZfASN1::save(s, data);
    } else {
      Data::SK_PKCS8_MLKEM_EXPANDED data{
        .version = 0, .id = OIDs::MLKEM768,
        .key = ZuBSpan{bytes.data(), n}
      };
      ZfASN1::save(s, data);
    }
    ZuClear(bytes.data(), n);
    return {};
  }

  template <typename S>
  ZuUnion<void, ZeException> savePK(S &s) const { return PK_::save(s); }

  ZuUnion<ZmRef<PK>, ZeException> mkPK() const {
    auto bytes = ZmScratch(
      uint8_t, MLKEM768PublicSize, KEMScratchHeap);
    bytes.length(MLKEM768PublicSize);
    auto r = PK_::exportPK(bytes);
    if (r.template is<ZeException>())
      return ZuMv(r).template p<ZeException>();
    try {
      return ZmRef(new PK{bytes});
    } catch (const ZeException &e) {
      return e;
    }
  }

  ZuUnion<void, ZeException> decapsulate(
    ZuBSpan ciphertext, ZuSpan<uint8_t> secret) const {
    if (!Backend::pkey_mlkem768_decapsulate(key, ciphertext, secret))
      return ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 decapsulation failed");
    return {};
  }
};
ZuDerive(SK_MLKEM768_Heap, (ZmHeap<"Ztls.SK_MLKEM768", SK_MLKEM768_<>>));
ZuDerive(SK_MLKEM768, (SK_MLKEM768_<SK_MLKEM768_Heap>));

enum {
  HybridSeedSize = X25519KeySize,
  HybridPublicSize = MLKEM768PublicSize + X25519KeySize,
  HybridCiphertextSize = MLKEM768CiphertextSize + X25519KeySize,
  HybridSecretSize = MLKEM768SecretSize,
  HybridExpandedSize = MLKEM768SeedSize + X25519KeySize
};

inline bool hybridCombine_(
  ZuBSpan pq, ZuBSpan x, ZuBSpan ephemeral, ZuBSpan recipient,
  ZuSpan<uint8_t> secret)
{
  constexpr uint8_t label[] = {0x5c, 0x2e, 0x2f, 0x2f, 0x5e, 0x5c};
  ZuBSpan input[] = {pq, x, ephemeral, recipient, label};
  return Backend::sha3_256(input, secret);
}

template <typename Heap = ZuVoid>
struct PK_MLKEM768_X25519_ : public Heap, public AnyPK {
  enum { Secret = 0 };

  uint8_t xPublic[X25519KeySize];

protected:
  PK_MLKEM768_X25519_() { }

public:
  PK_MLKEM768_X25519_(ZuBSpan pubKey) {
    if (pubKey.length() != HybridPublicSize ||
	!Backend::pkey_mlkem768_import_public(key,
	  {pubKey.data(), MLKEM768PublicSize}))
      throw ZeEXCEPT(Error, "ZtlsKEM", "hybrid public key import failed");
    memcpy(xPublic, pubKey.data() + MLKEM768PublicSize, X25519KeySize);
  }

  ZuUnion<void, ZeException> exportPK(ZuSpan<uint8_t> pubKey) const {
    if (pubKey.length() != HybridPublicSize ||
	!Backend::pkey_mlkem768_export_public(key,
	  {pubKey.data(), MLKEM768PublicSize}))
      return ZeEXCEPT(Error, "ZtlsKEM", "hybrid public key export failed");
    memcpy(pubKey.data() + MLKEM768PublicSize, xPublic, X25519KeySize);
    return {};
  }

  ZuUnion<void, ZeException> encapsulate(Random &rng,
    ZuSpan<uint8_t> ciphertext, ZuSpan<uint8_t> secret) const {
    if (ciphertext.length() != HybridCiphertextSize ||
	secret.length() != HybridSecretSize)
      return ZeEXCEPT(Error, "ZtlsKEM", "invalid hybrid output size");
    uint8_t shared[MLKEM768SecretSize + X25519KeySize];
    ZuGuard clear{[&shared]() { ZuClear(shared, sizeof(shared)); }};
    if (!Backend::pkey_mlkem768_encapsulate(key,
	{ciphertext.data(), MLKEM768CiphertextSize},
	{shared, MLKEM768SecretSize}))
      return ZeEXCEPT(Error, "ZtlsKEM", "hybrid ML-KEM encapsulation failed");
    try {
      SK_X25519 ephemeral{rng};
      auto ephPub = ZuSpan<uint8_t>{
	ciphertext.data() + MLKEM768CiphertextSize, X25519KeySize};
      auto r = ephemeral.exportPK(ephPub);
      if (r.template is<ZeException>())
	return ZuMv(r).template p<ZeException>();
      r = ephemeral.agree(xPublic,
	{shared + MLKEM768SecretSize, X25519KeySize});
      if (r.template is<ZeException>())
	return ZuMv(r).template p<ZeException>();
      if (!hybridCombine_(
	{shared, MLKEM768SecretSize},
	{shared + MLKEM768SecretSize, X25519KeySize},
	ephPub, xPublic, secret))
	return ZeEXCEPT(Error, "ZtlsKEM", "hybrid SHA3-256 failed");
      return {};
    } catch (const ZeException &e) {
      return e;
    }
  }
};
ZuDerive(PK_MLKEM768_X25519_Heap,
  (ZmHeap<"Ztls.PK_MLKEM768_X25519", PK_MLKEM768_X25519_<>>));
ZuDerive(PK_MLKEM768_X25519,
  (PK_MLKEM768_X25519_<PK_MLKEM768_X25519_Heap>));

template <typename Heap = ZuVoid>
struct SK_MLKEM768_X25519_ : public PK_MLKEM768_X25519_<Heap> {
  enum { Secret = 1 };
  using PK = PK_MLKEM768_X25519;
  using PK_ = PK_MLKEM768_X25519_<Heap>;
  using PK_::key;
  using PK_::xPublic;

  Backend::PKey *xKey = nullptr;

  SK_MLKEM768_X25519_(ZuBSpan seed) {
    if (seed.length() != HybridSeedSize)
      throw ZeEXCEPT(Error, "ZtlsKEM", "invalid hybrid identity seed");
    uint8_t expanded[HybridExpandedSize];
    ZuGuard clear{[&expanded]() { ZuClear(expanded, sizeof(expanded)); }};
    if (!Backend::shake256(seed, expanded))
      throw ZeEXCEPT(Error, "ZtlsKEM", "hybrid seed expansion failed");
    xKey = Backend::pkey_new();
    ZuGuard release{[this]() { Backend::pkey_free(xKey); }};
    if (!Backend::pkey_mlkem768_import_seed(key,
	{expanded, MLKEM768SeedSize}) ||
	!Backend::pkey_x25519_import_private(xKey,
	  {expanded + MLKEM768SeedSize, X25519KeySize}) ||
	!Backend::pkey_x25519_export_public(xKey, xPublic))
      throw ZeEXCEPT(Error, "ZtlsKEM", "hybrid identity import failed");
    release.cancel();
  }

  ~SK_MLKEM768_X25519_() { Backend::pkey_free(xKey); }

  ZuUnion<void, ZeException> decapsulate(
    ZuBSpan ciphertext, ZuSpan<uint8_t> secret) const {
    if (ciphertext.length() != HybridCiphertextSize ||
	secret.length() != HybridSecretSize)
      return ZeEXCEPT(Error, "ZtlsKEM", "invalid hybrid input size");
    uint8_t shared[MLKEM768SecretSize + X25519KeySize];
    ZuGuard clear{[&shared]() { ZuClear(shared, sizeof(shared)); }};
    if (!Backend::pkey_mlkem768_decapsulate(key,
	{ciphertext.data(), MLKEM768CiphertextSize},
	{shared, MLKEM768SecretSize}))
      return ZeEXCEPT(Error, "ZtlsKEM", "hybrid ML-KEM decapsulation failed");
    auto ephPub = ZuBSpan{
      ciphertext.data() + MLKEM768CiphertextSize, X25519KeySize};
    if (!Backend::pkey_x25519_agree(xKey, ephPub,
	{shared + MLKEM768SecretSize, X25519KeySize}))
      return ZeEXCEPT(Error, "ZtlsKEM", "hybrid X25519 agreement failed");
    if (!hybridCombine_(
	{shared, MLKEM768SecretSize},
	{shared + MLKEM768SecretSize, X25519KeySize},
	ephPub, xPublic, secret))
      return ZeEXCEPT(Error, "ZtlsKEM", "hybrid SHA3-256 failed");
    return {};
  }
};
ZuDerive(SK_MLKEM768_X25519_Heap,
  (ZmHeap<"Ztls.SK_MLKEM768_X25519", SK_MLKEM768_X25519_<>>));
ZuDerive(SK_MLKEM768_X25519,
  (SK_MLKEM768_X25519_<SK_MLKEM768_X25519_Heap>));

} // Ztls::PK

#endif /* ZtlsKEM_HH */
