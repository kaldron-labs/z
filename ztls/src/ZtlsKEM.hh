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

template <bool Enabled = true>
struct MLKEM768Public_ : public ZmNHeap<MLKEM768Public_<Enabled>, Enabled>, public AnyPK {
friend inline ZuStringT<"Ztls.MLKEM768Public"> ZmNHeapID(MLKEM768Public_ *);

  enum { Secret = 0 };
protected:
  MLKEM768Public_() { }
public:
  MLKEM768Public_(ZuBSpan pubKey)
  {
    if (!mlkem768ImportPublic(key, pubKey))
      throw ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 public key import failed");
  }

  template <typename S> ZuUnion<void, ZeException> save(S &s) const
  {
    auto pubKey = ZmScratch(uint8_t, MLKEM768PublicSize, KEMScratchHeap);
    pubKey.length(MLKEM768PublicSize);
    if (!mlkem768ExportPublic(key, pubKey))
      return ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 public key export failed");
    Data::X509PublicMLKEM data{.id = OIDs::MLKEM768, .pubKey = pubKey};
    ZfASN1::save(s, data);
    return {};
  }

  ZuUnion<void, ZeException> exportPK(ZuSpan<uint8_t> pubKey) const
  {
    if (!mlkem768ExportPublic(key, pubKey))
      return ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 public key export failed");
    return {};
  }

  ZuUnion<void, ZeException>
  encapsulate(Random &rng, ZuSpan<uint8_t> ciphertext, ZuSpan<uint8_t> secret) const
  {
    (void)rng;
    if (!mlkem768Encapsulate(key, ciphertext, secret))
      return ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 encapsulation failed");
    return {};
  }
};
using MLKEM768Public = MLKEM768Public_<>;

struct MLKEM768Private : public ZmNHeap<MLKEM768Private>, public MLKEM768Public_<false> {
friend inline ZuStringT<"Ztls.MLKEM768Private"> ZmNHeapID(MLKEM768Private *);

  enum { Secret = 1 };
  using PK = MLKEM768Public;
  using PK_ = MLKEM768Public_<false>;
  using PK_::key;

  MLKEM768Private(Random &rng)
  {
    (void)rng;
    if (!mlkem768Generate(key))
      throw ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 key generation failed");
  }

  MLKEM768Private(ZuBSpan bytes)
  {
    bool ok = bytes.length() == MLKEM768SeedSize ? mlkem768ImportSeed(key, bytes) :
						   mlkem768ImportPrivate(key, bytes);
    if (!ok) throw ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 private key import failed");
  }

  template <typename S> ZuUnion<void, ZeException> save(S &s) const
  {
    auto bytes = ZmScratch(uint8_t, MLKEM768PrivateSize, KEMScratchHeap);
    bytes.length(MLKEM768PrivateSize);
    size_t n = 0;
    if (!mlkem768ExportPrivate(key, bytes, &n)) {
      ZuClear(bytes);
      return ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 private key export failed");
    }
    if (n == MLKEM768SeedSize) {
      Data::PKCS8PrivateMLKEMSeed data{
	.version = 0, .id = OIDs::MLKEM768, .seed = ZuBSpan{bytes.data(), n}
	    };
      ZfASN1::save(s, data);
    } else {
      Data::PKCS8PrivateMLKEMExpanded data{
	.version = 0, .id = OIDs::MLKEM768, .key = ZuBSpan{bytes.data(), n}
	   };
      ZfASN1::save(s, data);
    }
    ZuClear(bytes.data(), n);
    return {};
  }

  template <typename S> ZuUnion<void, ZeException> savePK(S &s) const { return PK_::save(s); }

  ZuUnion<ZmRef<PK>, ZeException> mkPK() const
  {
    auto bytes = ZmScratch(uint8_t, MLKEM768PublicSize, KEMScratchHeap);
    bytes.length(MLKEM768PublicSize);
    auto r = PK_::exportPK(bytes);
    if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
    try {
      return ZmRef(new PK{bytes});
    } catch (const ZeException &e) {
      return e;
    }
  }

  ZuUnion<void, ZeException> decapsulate(ZuBSpan ciphertext, ZuSpan<uint8_t> secret) const
  {
    if (!mlkem768Decapsulate(key, ciphertext, secret))
      return ZeEXCEPT(Error, "ZtlsKEM", "ML-KEM-768 decapsulation failed");
    return {};
  }
};

enum {
  HybridSeedSize = X25519KeySize,
  HybridPublicSize = MLKEM768PublicSize + X25519KeySize,
  HybridCiphertextSize = MLKEM768CiphertextSize + X25519KeySize,
  HybridSecretSize = MLKEM768SecretSize,
  HybridExpandedSize = MLKEM768SeedSize + X25519KeySize
};

inline bool
hybridCombine(ZuBSpan pq, ZuBSpan x, ZuBSpan ephemeral, ZuBSpan recipient, ZuSpan<uint8_t> secret)
{
  constexpr uint8_t label[] = {0x5c, 0x2e, 0x2f, 0x2f, 0x5e, 0x5c};
  ZuBSpan	    input[] = {pq, x, ephemeral, recipient, label};
  return Ztls::sha3_256(input, secret);
}

template <bool Enabled = true>
struct MLKEM768X25519Public_ :
public ZmNHeap<MLKEM768X25519Public_<Enabled>, Enabled>,
  public AnyPK {
friend inline ZuStringT<"Ztls.MLKEM768X25519Public"> ZmNHeapID(MLKEM768X25519Public_ *);

  enum { Secret = 0 };

  uint8_t	xPublic[X25519KeySize];
protected:
  MLKEM768X25519Public_() { }
public:
  MLKEM768X25519Public_(ZuBSpan pubKey)
  {
    if (pubKey.length() != HybridPublicSize ||
      !mlkem768ImportPublic(key, {pubKey.data(), MLKEM768PublicSize}))
      throw ZeEXCEPT(Error, "ZtlsKEM", "hybrid public key import failed");
    memcpy(xPublic, pubKey.data() + MLKEM768PublicSize, X25519KeySize);
  }

  ZuUnion<void, ZeException> exportPK(ZuSpan<uint8_t> pubKey) const
  {
    if (pubKey.length() != HybridPublicSize ||
      !mlkem768ExportPublic(key, {pubKey.data(), MLKEM768PublicSize}))
      return ZeEXCEPT(Error, "ZtlsKEM", "hybrid public key export failed");
    memcpy(pubKey.data() + MLKEM768PublicSize, xPublic, X25519KeySize);
    return {};
  }

  ZuUnion<void, ZeException>
  encapsulate(Random &rng, ZuSpan<uint8_t> ciphertext, ZuSpan<uint8_t> secret) const
  {
    if (ciphertext.length() != HybridCiphertextSize || secret.length() != HybridSecretSize)
      return ZeEXCEPT(Error, "ZtlsKEM", "invalid hybrid output size");
    uint8_t shared[MLKEM768SecretSize + X25519KeySize];
    ZuGuard clear{[&shared]() { ZuClear(shared, sizeof(shared)); }};
    if (!mlkem768Encapsulate(
	  key, {ciphertext.data(), MLKEM768CiphertextSize}, {shared, MLKEM768SecretSize}))
      return ZeEXCEPT(Error, "ZtlsKEM", "hybrid ML-KEM encapsulation failed");
    try {
      X25519Private ephemeral{rng};
      auto ephPub = ZuSpan<uint8_t>{ciphertext.data() + MLKEM768CiphertextSize, X25519KeySize};
      auto r = ephemeral.exportPK(ephPub);
      if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
      r = ephemeral.agree(xPublic, {shared + MLKEM768SecretSize, X25519KeySize});
      if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
      if (!hybridCombine(
	    {shared, MLKEM768SecretSize}, {shared + MLKEM768SecretSize, X25519KeySize}, ephPub,
	    xPublic, secret))
	return ZeEXCEPT(Error, "ZtlsKEM", "hybrid SHA3-256 failed");
      return {};
    } catch (const ZeException &e) {
      return e;
    }
  }
};
using MLKEM768X25519Public = MLKEM768X25519Public_<>;

struct MLKEM768X25519Private :
public ZmNHeap<MLKEM768X25519Private>,
  public MLKEM768X25519Public_<false> {
friend inline ZuStringT<"Ztls.MLKEM768X25519Private"> ZmNHeapID(MLKEM768X25519Private *);

  enum { Secret = 1 };
  using PK = MLKEM768X25519Public;
  using PK_ = MLKEM768X25519Public_<false>;
  using PK_::key;
  using PK_::xPublic;

  Key	xKey;

  MLKEM768X25519Private(ZuBSpan seed)
  {
    if (seed.length() != HybridSeedSize)
      throw ZeEXCEPT(Error, "ZtlsKEM", "invalid hybrid identity seed");
    uint8_t expanded[HybridExpandedSize];
    ZuGuard clear{[&expanded]() { ZuClear(expanded, sizeof(expanded)); }};
    if (!Ztls::shake256(seed, expanded))
      throw ZeEXCEPT(Error, "ZtlsKEM", "hybrid seed expansion failed");
    if (!mlkem768ImportSeed(key, {expanded, MLKEM768SeedSize}) ||
      !x25519ImportPrivate(xKey, {expanded + MLKEM768SeedSize, X25519KeySize}) ||
      !x25519ExportPublic(xKey, xPublic))
      throw ZeEXCEPT(Error, "ZtlsKEM", "hybrid identity import failed");
  }

  ZuUnion<void, ZeException> decapsulate(ZuBSpan ciphertext, ZuSpan<uint8_t> secret) const
  {
    if (ciphertext.length() != HybridCiphertextSize || secret.length() != HybridSecretSize)
      return ZeEXCEPT(Error, "ZtlsKEM", "invalid hybrid input size");
    uint8_t shared[MLKEM768SecretSize + X25519KeySize];
    ZuGuard clear{[&shared]() { ZuClear(shared, sizeof(shared)); }};
    if (!mlkem768Decapsulate(
	  key, {ciphertext.data(), MLKEM768CiphertextSize}, {shared, MLKEM768SecretSize}))
      return ZeEXCEPT(Error, "ZtlsKEM", "hybrid ML-KEM decapsulation failed");
    auto ephPub = ZuBSpan{ciphertext.data() + MLKEM768CiphertextSize, X25519KeySize};
    if (!x25519Agree(xKey, ephPub, {shared + MLKEM768SecretSize, X25519KeySize}))
      return ZeEXCEPT(Error, "ZtlsKEM", "hybrid X25519 agreement failed");
    if (!hybridCombine(
	  {shared, MLKEM768SecretSize}, {shared + MLKEM768SecretSize, X25519KeySize}, ephPub,
	  xPublic, secret))
      return ZeEXCEPT(Error, "ZtlsKEM", "hybrid SHA3-256 failed");
    return {};
  }
};

} // namespace Ztls::PK

// ZtlsKEM_HH
#endif
