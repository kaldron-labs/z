//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OpenSSL-backed ML-KEM-768 key encapsulation and ASN.1 key serialization

#ifndef ZtlsKEM_HH
#define ZtlsKEM_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

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

  ZuUnion<void, ZeException> encapsulate(Random &rng,
    ZuSpan<uint8_t> ciphertext, ZuSpan<uint8_t> secret) const {
    (void)rng;
    if (!Backend::pkey_mlkem768_encapsulate(key, ciphertext, secret))
      return ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 encapsulation failed");
    return {};
  }
};
using PK_MLKEM768_Heap = ZmHeap<"Ztls.PK_MLKEM768", PK_MLKEM768_<>>;
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
      ZuClear(bytes.data(), bytes.length());
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
using SK_MLKEM768_Heap = ZmHeap<"Ztls.SK_MLKEM768", SK_MLKEM768_<>>;
ZuDerive(SK_MLKEM768, (SK_MLKEM768_<SK_MLKEM768_Heap>));

} // Ztls::PK

#endif /* ZtlsKEM_HH */
