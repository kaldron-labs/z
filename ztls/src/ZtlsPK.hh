//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OpenSSL-backed public/private keys
// - private key generate/save/load/sign
// - public key save/load/verify
// - reads any of PKCS#8, PKCS#1, SEC1, X509
// - writes PKCS#8 for private keys, X509 for public keys
// - DER, b64 and PEM support
// - includes raw-key support for ED25519 keys
// - SK* - private (secret) key related
// - PK* - public key related

#ifndef ZtlsPK_HH
#define ZtlsPK_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmCodec.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZfASN1.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPKOps.hh>
#include <zlib/ZtlsPKData.hh>
#include <zlib/ZtlsRandom.hh>

namespace Ztls::PK {

struct AnyKey;

namespace Load_ {

  Ztls_Extern int keyType(ZuCSpan);
  Ztls_Extern ZuUnion<ZmRef<AnyKey>, ZeException> loadMLKEM768PK(ZuBSpan);
  Ztls_Extern ZuUnion<ZmRef<AnyKey>, ZeException> loadMLKEM768SK(ZuBSpan);

}

// save RSA public key as X.509
template <typename S> inline ZuUnion<void, ZeException> saveRSAPublic(S &s, const Key &key)
{
  unsigned n = rsaSize(key);
  auto	   modulus = ZmScratch(uint8_t, n);
  auto	   pubExp = ZmScratch(uint8_t, 8);
  modulus.length(n);
  pubExp.length(8);

  if (!rsaExportPublic(key, modulus, pubExp))
    return ZeEXCEPT(Error, "ZtlsPK", "RSA export failed");

  Data::X509PublicRSA data{
    .id = OIDs::PKCS1RSA,
    .rsa = {// PKCS#1
      modulus, pubExp}
  };

  ZfASN1::save(s, data);

  return {};
}

// save EC public key as X.509
template <typename S> inline ZuUnion<void, ZeException> saveECPublic(S &s, const Key &key)
{
  unsigned oidLen = ecOIDSize(key);
  auto	   oid = ZmScratch(uint8_t, oidLen);
  oid.length(oidLen);
  if (!ecExportOID(key, oid)) return ZeEXCEPT(Error, "ZtlsPK", "EC OID export failed");

  unsigned pubLen = ecPublicSize(key);
  auto	   pubKey = ZmScratch(uint8_t, pubLen);
  pubKey.length(pubLen);
  if (!ecExportPublic(key, pubKey))
    return ZeEXCEPT(Error, "ZtlsPK", "EC public key export failed");

  Data::X509PublicEC data{
    .id = OIDs::ECUnrestricted,
    .id2 = oid,
    .pubKey = pubKey,
  };

  ZfASN1::save(s, data);

  return {};
}

template <typename S> inline ZuUnion<void, ZeException> saveCSR_(S &s, const Key &key)
{
  struct Context {
    S	*s;
    static bool save(void *ptr, ZuBSpan data)
    {
      *static_cast<Context *>(ptr)->s << data;
      return true;
    }
  } context{&s};
  if (!csr(key, &context, Context::save))
    return ZeEXCEPT(Error, "ZtlsPK", "CSR generation failed");
  return {};
}

// any key
struct AnyKey : public ZmPolymorph { };

// any concrete libcrypto key
struct AnyPK : public AnyKey {
  Key	key;
};

// RSA public key
template <bool Enabled = true>
struct RSAPublic_ : public ZmNHeap<RSAPublic_<Enabled>, Enabled>, public AnyPK {
friend inline ZuStringT<"Ztls.RSAPublic"> ZmNHeapID(RSAPublic_ *);

  enum { Secret = 1 };
protected:
  RSAPublic_() { }
public:
  // load from PKCS#1 public key data
  RSAPublic_(const Data::PKCS1Public &data)
  {
    if (!rsaImportPublic(key, data.modulus, data.pubExp)) {
      throw ZeEXCEPT(Error, "ZtlsPK", "RSA public key import failed");
    }
  }

  // save public key
  template <typename S> ZuUnion<void, ZeException> save(S &s) const
  { return saveRSAPublic(s, key); }

  // verify signature
  template <MDType MDType = SHA256>
  ZuUnion<bool, ZeException> verify(ZuBSpan data, ZuBSpan signature)
  {
    if (signature.length() != rsaSize(key))
      return ZeEXCEPT(Error, "ZtlsPK", "invalid RSA signature");
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    return verifyData(key, MDType, data, signature);
  }

  ZuUnion<size_t, ZeException>
  oaepEncrypt(ZuBSpan plaintext, ZuBSpan label, ZuSpan<uint8_t> ciphertext) const
  {
    size_t n = 0;
    if (!rsaOaepEncrypt(key, plaintext, label, ciphertext, &n))
      return ZeEXCEPT(Error, "ZtlsPK", "RSA OAEP encryption failed");
    return n;
  }
};
using RSAPublic = RSAPublic_<>;

// RSA private key
struct RSAPrivate : public ZmNHeap<RSAPrivate>, public RSAPublic_<false> {
friend inline ZuStringT<"Ztls.RSAPrivate"> ZmNHeapID(RSAPrivate *);

  enum { Secret = 1 };
  using PK = RSAPublic;
  using PK_ = RSAPublic_<false>;
  using PK_::key;

  // generate key
  RSAPrivate(Random &rng, unsigned bits)
  {
    (void)rng;
    if (!rsaGenerate(key, bits)) throw ZeEXCEPT(Error, "ZtlsPK", "RSA key generation failed");
  }

  // load key from PKCS#1 data
  RSAPrivate(const Data::PKCS1Private &data)
  {
    if (!rsaImportPrivate(
	  key, data.modulus, data.pubExp, data.prvExp, data.prime1, data.prime2, data.exp1,
	  data.exp2, data.coeff))
      throw ZeEXCEPT(Error, "ZtlsPK", "RSA private key import failed");
  }

  RSAPrivate(
    ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp, ZuBSpan coeff, ZuBSpan prime1,
    ZuBSpan prime2)
  {
    if (!rsaImportSSHPrivate(key, modulus, pubExp, prvExp, coeff, prime1, prime2))
      throw ZeEXCEPT(Error, "ZtlsPK", "SSH RSA private key import failed");
  }

  // save private key
  template <typename S> ZuUnion<void, ZeException> save(S &s) const
  {
    unsigned n = rsaSize(key);

    auto     modulus = ZmScratch(uint8_t, n);
    auto     pubExp = ZmScratch(uint8_t, 8);
    auto     prvExp = ZmScratch(uint8_t, n);
    unsigned p = n >> 1;
    auto     prime1 = ZmScratch(uint8_t, p);
    auto     prime2 = ZmScratch(uint8_t, p);
    auto     exp1 = ZmScratch(uint8_t, p);
    auto     exp2 = ZmScratch(uint8_t, p);
    auto     coeff = ZmScratch(uint8_t, p);
    modulus.length(n);
    pubExp.length(8);
    prvExp.length(n);
    prime1.length(p);
    prime2.length(p);
    exp1.length(p);
    exp2.length(p);
    coeff.length(p);

    if (!rsaExportPrivate(key, modulus, pubExp, prvExp, prime1, prime2, exp1, exp2, coeff))
      return ZeEXCEPT(Error, "ZtlsPK", "RSA private key export failed");

    Data::PKCS8PrivateRSA data{
      .version = 0,
      .id = OIDs::PKCS1RSA,
      .rsa = {// PKCS#1
	0, modulus, pubExp, prvExp, prime1, prime2, exp1, exp2, coeff}
    };

    ZfASN1::save(s, data);

    return {};
  }

  // save public key
  template <typename S> ZuUnion<void, ZeException> savePK(S &s) const { return PK_::save(s); }

  template <typename S> ZuUnion<void, ZeException> saveCSR(S &s) const
  { return saveCSR_(s, key); }

  // create public key
  ZuUnion<ZmRef<PK>, ZeException> mkPK()
  {
    using namespace Data;

    auto buf = ZmScratch(char, DERBufSize);
    auto r = savePK(buf);
    if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
    auto data = ZfASN1::handler<X509PublicRSA>(buf).ctor();
    try {
      return ZmRef(new PK{data.rsa});
    } catch (const ZeException &e) {
      return e;
    }
  }

  ZuUnion<size_t, ZeException>
  oaepDecrypt(ZuBSpan ciphertext, ZuBSpan label, ZuSpan<uint8_t> plaintext) const
  {
    size_t n = 0;
    if (!rsaOaepDecrypt(key, ciphertext, label, plaintext, &n))
      return ZeEXCEPT(Error, "ZtlsPK", "RSA OAEP decryption failed");
    return n;
  }

  // sign data
  template <MDType MDType = SHA256, typename L>
  ZuUnion<void, ZeException> sign(Random &rng, ZuBSpan data, L &&l)
  {
    (void)rng;
    unsigned n = rsaSize(key);
    auto     signature = ZmScratch(uint8_t, n);
    signature.length(n);
    size_t k = 0;
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    if (!signData(key, MDType, data, signature, &k))
      return ZeEXCEPT(Error, "ZtlsPK", "RSA sign failed");
    signature.length(k);
    ZuFwd<L>(l)(signature);
    return {};
  }
};

// EC public key
template <bool Enabled = true>
struct ECPublic_ : public ZmNHeap<ECPublic_<Enabled>, Enabled>, public AnyPK {
friend inline ZuStringT<"Ztls.ECPublic"> ZmNHeapID(ECPublic_ *);

  enum { Secret = 0 };
protected:
  ECPublic_() { }
public:
  // load key from X509 data
  ECPublic_(ZuBSpan id_, ZuBSpan key)
  {
    if (!ecImportPublic(this->key, id_, key))
      throw ZeEXCEPT(Error, "ZtlsPK", "EC public key import failed");
  }

  // save public key
  template <typename S> ZuUnion<void, ZeException> save(S &s) const
  { return saveECPublic(s, key); }

  // verify signature
  template <MDType MDType = SHA256>
  ZuUnion<bool, ZeException> verify(ZuBSpan data, ZuBSpan signature)
  {
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    if (!verifyData(key, MDType, data, signature))
      return ZeEXCEPT(Error, "ZtlsPK", "EC verify failed");
    return true;
  }
};
using ECPublic = ECPublic_<>;

// EC private key
struct ECPrivate : public ZmNHeap<ECPrivate>, public ECPublic_<false> {
friend inline ZuStringT<"Ztls.ECPrivate"> ZmNHeapID(ECPrivate *);

  enum { Secret = 1 };
  using PK = ECPublic;
  using PK_ = ECPublic_<false>;
  using PK_::key;

  // generate key
  ECPrivate(Random &rng, ZuBSpan id_)
  {
    (void)rng;
    if (!ecGenerate(key, id_)) throw ZeEXCEPT(Error, "ZtlsPK", "EC key generation failed");
  }

  // load key from SEC1 / PKCS#8 data
  ECPrivate(Random &rng, ZuBSpan id_, ZuBSpan key)
  {
    (void)rng;
    if (!ecImportPrivate(this->key, id_, key))
      throw ZeEXCEPT(Error, "ZtlsPK", "EC private key import failed");
  }

  template <typename S> ZuUnion<void, ZeException> save(S &s) const
  {
    unsigned oidLen = ecOIDSize(key);
    auto     oid = ZmScratch(uint8_t, oidLen);
    oid.length(oidLen);
    if (!ecExportOID(key, oid)) return ZeEXCEPT(Error, "ZtlsPK", "EC OID export failed");

    unsigned n = ecKeySize(key);
    auto     prvKey = ZmScratch(uint8_t, n);
    prvKey.length(n);
    if (!ecExportPrivate(this->key, prvKey))
      return ZeEXCEPT(Error, "ZtlsPK", "EC private key export failed");

    unsigned pubLen = ecPublicSize(this->key);
    auto     pubKey = ZmScratch(uint8_t, pubLen);
    pubKey.length(pubLen);
    if (!ecExportPublic(this->key, pubKey))
      return ZeEXCEPT(Error, "ZtlsPK", "EC public key export failed");

    Data::PKCS8PrivateEC data{
      .version = 0,
      .id = OIDs::ECUnrestricted,
      .id2 = oid,
      .ec = {
	     // SEC1
	.version = 1,
	     .key = prvKey,
	     .id = {},	 // ID (unused when embedded in PKCS#8 wrapper)
	.pubKey = pubKey // optional public key
      }
    };

    ZfASN1::save(s, data);

    return {};
  }

  // save public key
  template <typename S> ZuUnion<void, ZeException> savePK(S &s) const { return PK_::save(s); }

  template <typename S> ZuUnion<void, ZeException> saveCSR(S &s) const
  { return saveCSR_(s, key); }

  // create public key
  ZuUnion<ZmRef<PK>, ZeException> mkPK()
  {
    using namespace Data;

    auto buf = ZmScratch(char, DERBufSize);
    auto r = savePK(buf);
    if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
    auto data = ZfASN1::handler<X509PublicEC>(buf).ctor();
    try {
      return ZmRef(new PK{data.id2, data.pubKey});
    } catch (const ZeException &e) {
      return e;
    }
  }

  // sign data
  template <MDType MDType = SHA256, typename L>
  ZuUnion<void, ZeException> sign(Random &rng, ZuBSpan data, L &&l)
  {
    (void)rng;
    unsigned n = ecKeySize(key);
    n += ZfASN1::len_uint(n) + 2; // ASN.1 Integer
    n = (n << 1);		  // x2
    n += ZfASN1::len_uint(n) + 1; // ASN.1 Sequence
    auto signature = ZmScratch(uint8_t, n);
    signature.length(n);
    size_t k = 0;
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    if (!signData(key, MDType, data, signature, &k))
      return ZeEXCEPT(Error, "ZtlsPK", "EC sign failed");
    signature.length(k);
    ZuFwd<L>(l)(signature);
    return {};
  }
};

// ED25519 public key
template <bool Enabled = true>
struct Ed25519Public_ : public ZmNHeap<Ed25519Public_<Enabled>, Enabled>, public AnyPK {
friend inline ZuStringT<"Ztls.Ed25519Public"> ZmNHeapID(Ed25519Public_ *);

  enum { Secret = 0 };
protected:
  Ed25519Public_() { }
public:
  // load key from X509 data
  Ed25519Public_(ZuBSpan key)
  {
    if (!ed25519ImportPublic(this->key, key))
      throw ZeEXCEPT(Error, "ZtlsPK", "ED25519 public key import failed");
  }

  ZuUnion<void, ZeException> exportPK(ZuSpan<uint8_t> pubKey) const
  {
    if (!ed25519ExportPublic(this->key, pubKey))
      return ZeEXCEPT(Error, "ZtlsPK", "ED25519 public key export failed");
    return {};
  }

  ZuUnion<void, ZeException> exportX25519(ZuSpan<uint8_t> pubKey) const
  {
    uint8_t edPublic[32];
    if (!ed25519ExportPublic(this->key, edPublic) || !ed25519ToX25519(edPublic, pubKey))
      return ZeEXCEPT(Error, "ZtlsPK", "ED25519 conversion failed");
    return {};
  }

  // save public key
  template <typename S> ZuUnion<void, ZeException> save(S &s) const
  {
    uint8_t pubKey[32];
    if (!ed25519ExportPublic(this->key, pubKey))
      return ZeEXCEPT(Error, "ZtlsPK", "ED25519 public key export failed");

    Data::X509Public25519 data{
      .id = OIDs::ED25519, .pubKey = ZuBSpan{pubKey, sizeof(pubKey)}
    };

    ZfASN1::save(s, data);

    return {};
  }

  // verify signature
  template <MDType MDType = SHA256>
  ZuUnion<bool, ZeException> verify(ZuBSpan data, ZuBSpan signature)
  {
    if (signature.length() != 64) return ZeEXCEPT(Error, "ZtlsPK", "invalid ED25519 signature");
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    return verifyData(this->key, MDType, data, signature);
  }
};
using Ed25519Public = Ed25519Public_<>;

// ED25519 private key
struct Ed25519Private : public ZmNHeap<Ed25519Private>, public Ed25519Public_<false> {
friend inline ZuStringT<"Ztls.Ed25519Private"> ZmNHeapID(Ed25519Private *);

  enum { Secret = 1 };
  using PK = Ed25519Public;
  using PK_ = Ed25519Public_<false>;
  using PK_::key;

  // generate key
  Ed25519Private(Random &rng)
  {
    (void)rng;
    if (!ed25519Generate(key)) throw ZeEXCEPT(Error, "ZtlsPK", "ED25519 key generation failed");
  }

  // load key from PKCS#8 data
  Ed25519Private(ZuBSpan key)
  {
    if (!ed25519ImportPrivate(this->key, key))
      throw ZeEXCEPT(Error, "ZtlsPK", "ED25519 private key import failed");
  }

  ZuUnion<void, ZeException> exportSK(ZuSpan<uint8_t> seed) const
  {
    if (!ed25519ExportPrivate(key, seed))
      return ZeEXCEPT(Error, "ZtlsPK", "ED25519 private key export failed");
    return {};
  }

  // save private key
  template <typename S> ZuUnion<void, ZeException> save(S &s) const
  {
    uint8_t prvKey[32];
    if (!ed25519ExportPrivate(key, prvKey))
      return ZeEXCEPT(Error, "ZtlsPK", "ED25519 private key export failed");

    Data::PKCS8Private25519 data{
      .version = 0, .id = OIDs::ED25519, .key = ZuBSpan{prvKey, sizeof(prvKey)}
    };

    ZfASN1::save(s, data);

    return {};
  }

  // save public key
  template <typename S> ZuUnion<void, ZeException> savePK(S &s) const { return PK_::save(s); }

  template <typename S> ZuUnion<void, ZeException> saveCSR(S &s) const
  { return saveCSR_(s, key); }

  // create public key
  ZuUnion<ZmRef<PK>, ZeException> mkPK()
  {
    using namespace Data;

    auto buf = ZmScratch(char, DERBufSize);
    auto r = savePK(buf);
    if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
    auto data = ZfASN1::handler<X509Public25519>(buf).ctor();
    try {
      return ZmRef(new PK{data.pubKey});
    } catch (const ZeException &e) {
      return e;
    }
  }

  // sign data
  template <MDType MDType = SHA256, typename L>
  ZuUnion<void, ZeException> sign(Random &, ZuBSpan data, L &&l)
  {
    uint8_t signature[64];
    size_t  k = 0;
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    if (
      !signData(key, MDType, data, {signature, sizeof(signature)}, &k) || k != sizeof(signature))
      return ZeEXCEPT(Error, "ZtlsPK", "ED25519 sign failed");
    ZuFwd<L>(l)(ZuBSpan{signature, sizeof(signature)});
    return {};
  }
};

template <bool Enabled = true>
struct X25519Public_ : public ZmNHeap<X25519Public_<Enabled>, Enabled>, public AnyPK {
friend inline ZuStringT<"Ztls.X25519Public"> ZmNHeapID(X25519Public_ *);

  enum { Secret = 0 };
protected:
  X25519Public_() { }
public:
  X25519Public_(ZuBSpan pubKey)
  {
    if (!x25519ImportPublic(key, pubKey))
      throw ZeEXCEPT(Error, "ZtlsPK", "X25519 public key import failed");
  }

  ZuUnion<void, ZeException> exportPK(ZuSpan<uint8_t> pubKey) const
  {
    if (!x25519ExportPublic(key, pubKey))
      return ZeEXCEPT(Error, "ZtlsPK", "X25519 public key export failed");
    return {};
  }

  template <typename S> ZuUnion<void, ZeException> save(S &s) const
  {
    uint8_t pubKey[X25519KeySize];
    auto    r = exportPK(pubKey);
    if (r.template is<ZeException>()) return r;
    Data::X509Public25519 data{.id = OIDs::X25519, .pubKey = pubKey};
    ZfASN1::save(s, data);
    return {};
  }
};
using X25519Public = X25519Public_<>;

struct X25519Private : public ZmNHeap<X25519Private>, public X25519Public_<false> {
friend inline ZuStringT<"Ztls.X25519Private"> ZmNHeapID(X25519Private *);

  enum { Secret = 1 };
  using PK = X25519Public;
  using PK_ = X25519Public_<false>;
  using PK_::key;

  X25519Private(Random &rng)
  {
    (void)rng;
    if (!x25519Generate(key)) throw ZeEXCEPT(Error, "ZtlsPK", "X25519 key generation failed");
  }

  X25519Private(ZuBSpan seed)
  {
    if (!x25519ImportPrivate(key, seed))
      throw ZeEXCEPT(Error, "ZtlsPK", "X25519 private key import failed");
  }

  ZuUnion<void, ZeException> exportSK(ZuSpan<uint8_t> seed) const
  {
    if (!x25519ExportPrivate(key, seed))
      return ZeEXCEPT(Error, "ZtlsPK", "X25519 private key export failed");
    return {};
  }

  template <typename S> ZuUnion<void, ZeException> save(S &s) const
  {
    uint8_t seed[X25519KeySize];
    ZuGuard clear{[&seed]() { ZuClear(seed, sizeof(seed)); }};
    auto    r = exportSK(seed);
    if (r.template is<ZeException>()) return r;
    Data::PKCS8Private25519 data{.version = 0, .id = OIDs::X25519, .key = seed};
    ZfASN1::save(s, data);
    return {};
  }

  template <typename S> ZuUnion<void, ZeException> savePK(S &s) const { return PK_::save(s); }

  ZuUnion<ZmRef<PK>, ZeException> mkPK() const
  {
    uint8_t pubKey[X25519KeySize];
    auto    r = PK_::exportPK(pubKey);
    if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
    try {
      return ZmRef(new PK{pubKey});
    } catch (const ZeException &e) {
      return e;
    }
  }

  ZuUnion<void, ZeException> agree(ZuBSpan peerPublic, ZuSpan<uint8_t> secret) const
  {
    if (!x25519Agree(key, peerPublic, secret))
      return ZeEXCEPT(Error, "ZtlsPK", "X25519 agreement failed");
    return {};
  }
};

template <typename Impl> struct Load {
  ZuInline Impl *impl() { return static_cast<Impl *>(this); }
  ZuInline const Impl *impl() const { return static_cast<const Impl *>(this); }

  ZuUnion<ZmRef<AnyKey>, ZeException> loadB64(ZuSpan<uint8_t> span, int hint)
  {
    // in-place-overwrite decoded
    span.trunc(ZuBase64::decode({&span[0], ZuBase64::declen(span.length())}, span));
    return impl()->load(span, hint);
  }

  ZuInline static constexpr bool isSpace(char c)
  { return ((c >= '\t' && c <= '\r') || c == ' '); }
  ZuUnion<ZmRef<AnyKey>, ZeException> loadPEM(ZuSpan<char> span)
  {
    using namespace Data;

    {
      // there can be leading and trailing junk in a PEM file...
      // find the header and offset the span
      unsigned offset, n;
      int      type = pemHead(span, offset, n);
      if (type < 0) goto bad;
      // ensure the span has enough space for a header and trailer
      if (span.length() < offset + ((n << 1) - 2)) goto bad;
      span.offset(offset + n);
      // find the trailer and truncate the span
      {
	int offset_ = pemTail(span, type);
	if (offset_ < 0) goto bad;
	span.trunc(offset_);
      }
      // remove all white space (in-place mutation)
      n = span.length();
      unsigned o = 0;
      for (unsigned i = 0; i < n; i++) {
	if (isSpace(span[i])) continue;
	if (ZuLikely(o == i)) {
	  o++;
	  continue;
	}
	span[o++] = span[i];
      }
      span.trunc(o);
      return loadB64(span, type);
    }
bad:
    return ZeEXCEPT(Error, "ZtlsPK", "invalid PEM format");
  }

  template <typename Path> ZuUnion<ZmRef<AnyKey>, ZeException> loadFile(const Path &path)
  {
    using namespace Data;

    ZiFile file;
    if (file.open(path, ZiFile::ReadOnly, 0666) != Zi::OK)
      return ZeEXCEPT(Error, "ZtlsPK", ([path, e = file.error()](auto &s) {
	s << "\"" << path << "\" open failed: " << e;
      }));
    auto size = file.size();
    if (size > BufSize)
      return ZeEXCEPT(Error, "ZtlsPK", ([path, e = file.error()](auto &s) {
	s << "\"" << path << "\" read failed: file too large";
      }));
    auto buf = ZmScratch(char, unsigned(size));
    if (file.read(buf.data(), size) < size)
      return ZeEXCEPT(Error, "ZtlsPK", ([path, e = file.error()](auto &s) {
	s << "\"" << path << "\" read failed: " << e;
      }));
    file.close();
    buf.length(unsigned(size));
    return loadPEM(buf);
  }
};

class LoadPK : public Load<LoadPK> {
public:
  LoadPK() { }
private:
  // identify public key format from ASN.1 TL leading data
  static int id(ZuSpan<uint8_t> span)
  {
    using namespace Data;
    using namespace ZfASN1;

    // X509   - Sequence { Sequence { ... } ... }
    // PKCS#1 - Sequence { Integer, ... }
    {
      auto [o, l] = loadTL<tagU(), Sequence>(span);
      if (o < 0) return -1;
      span.offset(o);
      span.trunc(l);
    }
    switch (span[0]) {
    case Sequence:
      return Type::X509Public;
    case Integer:
      return Type::PKCS1Public;
    }
    return -1;
  }
public:
  ZuUnion<ZmRef<AnyKey>, ZeException> load(ZuSpan<char> span, int hint = -1)
  {
    using namespace Data;

    int type = id(span);
    if (type < 0) return ZeEXCEPT(Error, "ZtlsPK", "invalid key data");
    if (hint >= 0 && type != hint) return ZeEXCEPT(Error, "ZtlsPK", "inconsistent key data");
    ZmRef<AnyKey> key;
    try {
      switch (type) {
      case Type::X509Public: {
	auto hdr = ZfASN1::handler<X509PublicHdr>(span).ctor();
	auto id = Load_::keyType(hdr.id);
	if (id < 0)
	  return ZeEXCEPT(Error, "ZtlsPK", ([id = ZtBArray(hdr.id)](auto &s) {
	    ZmHex::enc(id, [&s](ZuCSpan id) { s << "unknown X509 OID " << id; });
	  }));
	switch (id) {
	case 0: {
	  auto data = ZfASN1::handler<X509PublicRSA>(span).ctor();
	  key = new RSAPublic(data.rsa);
	} break;
	case 1: {
	  auto data = ZfASN1::handler<X509PublicEC>(span).ctor();
	  key = new ECPublic(data.id2, data.pubKey);
	} break;
	case 2: {
	  auto data = ZfASN1::handler<X509Public25519>(span).ctor();
	  key = new Ed25519Public(data.pubKey);
	} break;
	case 3: {
	  auto data = ZfASN1::handler<X509PublicMLKEM>(span).ctor();
	  return Load_::loadMLKEM768PK(data.pubKey);
	}
	case 4: {
	  auto data = ZfASN1::handler<X509Public25519>(span).ctor();
	  key = new X25519Public(data.pubKey);
	} break;
	}
      } break;
      case Type::PKCS1Public: {
	auto data = ZfASN1::handler<PKCS1Public>(span).ctor();
	key = new RSAPublic(data);
      } break;
      }
    } catch (const ZeException &e) {
      return e;
    }
    if (!key) return ZeEXCEPT(Error, "ZtlsPK", "unknown key type");
    return key;
  }
};

class LoadSK : public Load<LoadSK> {
public:
  LoadSK(Random &rng) : m_rng{rng} { }
private:
  // identify private key format from ASN.1 TL leading data
  static int id(ZuSpan<uint8_t> span)
  {
    using namespace Data;
    using namespace ZfASN1;

    // PKCS#8 - Sequence { Integer, Sequence, ... }
    // SEC1   - Sequence { Integer, OctetString, ... }
    // PKCS#1 - Sequence { Integer, Integer, ... }
    {
      auto [o, l] = loadTL<tagU(), Sequence>(span);
      if (o < 0) return -1;
      span.offset(o);
      span.trunc(l);
    }
    {
      auto [o, l] = loadTL<tagU(), Integer>(span);
      if (o < 0) return -1;
      span.offset(o + l);
    }
    switch (span[0]) {
    case Sequence:
      return Type::PKCS8Private;
    case OctetString:
      return Type::SEC1Private;
    case Integer:
      return Type::PKCS1Private;
    }
    return -1;
  }
public:
  ZuUnion<ZmRef<AnyKey>, ZeException> load(ZuSpan<char> span, int hint = -1)
  {
    using namespace Data;

    int type = id(span);
    if (type < 0) return ZeEXCEPT(Error, "ZtlsPK", "invalid key data");
    if (hint >= 0 && type != hint) return ZeEXCEPT(Error, "ZtlsPK", "inconsistent key data");
    ZmRef<AnyKey> key;
    try {
      switch (type) {
      case Type::PKCS8Private: {
	auto hdr = ZfASN1::handler<PKCS8PrivateHdr>(span).ctor();
	auto id = Load_::keyType(hdr.id);
	if (id < 0)
	  return ZeEXCEPT(Error, "ZtlsPK", ([id = ZtBArray(hdr.id)](auto &s) {
	    ZmHex::enc(id, [&s](ZuCSpan id) { s << "unknown PKCS#8 OID " << id; });
	  }));
	switch (id) {
	case 0: {
	  auto data = ZfASN1::handler<PKCS8PrivateRSA>(span).ctor();
	  key = new RSAPrivate(data.rsa);
	} break;
	case 1: {
	  auto data = ZfASN1::handler<PKCS8PrivateEC>(span).ctor();
	  key = new ECPrivate(m_rng, data.id2, data.ec.key);
	} break;
	case 2: {
	  auto data = ZfASN1::handler<PKCS8Private25519>(span).ctor();
	  key = new Ed25519Private(data.key);
	} break;
	case 3: {
	  auto data = ZfASN1::handler<PKCS8PrivateMLKEM>(span).ctor();
	  auto payload = data.key;
	  if (!payload.length())
	    return ZeEXCEPT(Error, "ZtlsKEM", "invalid ML-KEM-768 private key");
	  using namespace ZfASN1;
	  switch (payload[0]) {
	  case 0x80: {
	    auto [o, n] = loadTL<tagI(0), OctetString>(payload);
	    if (o >= 0 && n == 64)
	      return Load_::loadMLKEM768SK({payload.data() + o, unsigned(n)});
	  } break;
	  case OctetString: {
	    auto [o, n] = loadTL<tagU(), OctetString>(payload);
	    if (o >= 0 && n == 2400)
	      return Load_::loadMLKEM768SK({payload.data() + o, unsigned(n)});
	  } break;
	  case Sequence: {
	    auto both = ZfASN1::handler<PKCS8PrivateMLKEMBoth>(span).ctor();
	    if (both.both.seed.length() == 64 && both.both.key.length() == 2400)
	      return Load_::loadMLKEM768SK(both.both.seed);
	  } break;
	  }
	  return ZeEXCEPT(Error, "ZtlsKEM", "invalid ML-KEM-768 private key");
	}
	case 4: {
	  auto data = ZfASN1::handler<PKCS8Private25519>(span).ctor();
	  key = new X25519Private(data.key);
	} break;
	}
      } break;
      case Type::SEC1Private: {
	auto data = ZfASN1::handler<SEC1Private>(span).ctor();
	key = new ECPrivate(m_rng, data.id, data.key);
      }
      case Type::PKCS1Private: {
	auto data = ZfASN1::handler<PKCS1Private>(span).ctor();
	key = new RSAPrivate(data);
      }
      }
    } catch (const ZeException &e) {
      return e;
    }
    if (!key) return ZeEXCEPT(Error, "ZtlsPK", "unknown key type");
    return key;
  }
private:
  Random	&m_rng;
};

template <typename S, typename Key> ZuUnion<void, ZeException> saveB64(S &s, Key *key)
{
  using namespace Data;

  auto buf = ZmScratch(char, DERBufSize);
  auto r = key->save(buf);
  if (ZuUnlikely(r.template is<ZeException>())) return r;
  ZmBase64::enc(buf, [&s](ZuCSpan b64) { s << b64; });
  return {};
}

template <typename S, typename Key> ZuUnion<void, ZeException> savePEM(S &s, const Key *key)
{
  using namespace Data;

  auto buf = ZmScratch(char, BufSize);
  auto r = key->save(buf);
  if (ZuUnlikely(r.template is<ZeException>())) return r;
  ZmBase64::enc(
    buf, [&s](ZuCSpan b64)
  {
    if constexpr (Key::Secret)
      s << "-----BEGIN PRIVATE KEY-----\n";
    else
      s << "-----BEGIN PUBLIC KEY-----\n";
    unsigned n = b64.length();
    for (unsigned i = 0; i < n;) {
      unsigned j = i + 64;
      if (j > n) j = n;
      s << ZuCSpan(&b64[i], j - i) << '\n';
      i = j;
    }
    if constexpr (Key::Secret)
      s << "-----END PRIVATE KEY-----\n";
    else
      s << "-----END PUBLIC KEY-----\n";
  });
  return {};
}

template <typename Path, typename Key>
ZuUnion<void, ZeException> saveFile(const Path &path, const Key *key)
{
  using namespace Data;

  ZiFile file;
  if (file.open(path, ZiFile::Write, Key::Secret ? 0600 : 0666) != Zi::OK)
    return ZeEXCEPT(Error, "ZtlsPK", ([path, e = file.error()](auto &s) {
      s << "\"" << path << "\" - open failed: " << e;
    }));
  auto buf = ZmScratch(char, BufSize);
  auto r = savePEM(buf, key);
  if (ZuUnlikely(r.template is<ZeException>())) return r;
  if (file.write(buf.data(), buf.length()) != Zi::OK)
    return ZeEXCEPT(Error, "ZtlsPK", ([path, e = file.error()](auto &s) {
      s << "\"" << path << "\" - write failed: " << e;
    }));
  file.close();
  return {};
}

} // namespace Ztls::PK

// ZtlsPK_HH
#endif
