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
// - includes backend raw-key support for ED25519 keys
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

#include <zlib/ZiLog.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZtlsBackend.hh>

#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPK_Data.hh>

namespace Ztls::PK {

namespace Save_ {

inline auto mwb_error_(int e) {
  return [e](auto &s) {
    s << "bignum write failed: " << strerror_(e);
  };
}
// use a macro to get the right line number in the event
#define ZtlsPK_mwb_error(e) ZeEXCEPT(Error, "ZtlsPK", mwb_error_(e))

} // Save_

namespace Load_ {

ZtlsAPI int keyType(ZuCSpan);

inline auto mrb_error_(int e) {
  return [e](auto &s) {
    s << "bignum read failed: " << strerror_(e);
  };
}
// use a macro to get the right line number in the event
#define ZtlsPK_mrb_error(e) ZeEXCEPT(Error, "ZtlsPK", mrb_error_(e))
} // Load_

// save RSA public key as X.509
template <typename S>
inline ZuUnion<void, ZeException>
save_PK_RSA(S &s, const Backend::PKey *key) {
  using namespace Save_;

  unsigned n = Backend::pkey_rsa_size(key);
  auto modulus = ZmScratch(uint8_t, n);
  auto pubExp = ZmScratch(uint8_t, 8);
  modulus.length(n);
  pubExp.length(8);

  if (!Backend::pkey_rsa_export_public(key, modulus, pubExp))
    return ZeEXCEPT(Error, "ZtlsPK", "RSA export failed");

  Data::PK_X509_RSA data{
    .id = OIDs::PKCS1_RSA,
    .rsa = {	// PKCS#1
      modulus,
      pubExp
    }
  };

  ZfASN1::save(s, data);

  return {};
}

// save EC public key as X.509
template <typename S>
inline ZuUnion<void, ZeException>
save_PK_EC(S &s, const Backend::PKey *key) {
  using namespace Save_;

  unsigned oidLen = Backend::pkey_ec_oid_size(key);
  auto oid = ZmScratch(uint8_t, oidLen);
  oid.length(oidLen);
  if (!Backend::pkey_ec_export_oid(key, oid))
    return ZeEXCEPT(Error, "ZtlsPK", "EC OID export failed");

  unsigned pubLen = Backend::pkey_ec_public_size(key);
  auto pubKey = ZmScratch(uint8_t, pubLen);
  pubKey.length(pubLen);
  if (!Backend::pkey_ec_export_public(key, pubKey))
    return ZeEXCEPT(Error, "ZtlsPK", "EC public key export failed");

  Data::PK_X509_EC data{
    .id = OIDs::EC_ALG_UNRESTRICTED,
    .id2 = oid,
    .pubKey = pubKey,
  };

  ZfASN1::save(s, data);

  return {};
}

template <typename S>
inline ZuUnion<void, ZeException>
saveCSR_(S &s, const Backend::PKey *key) {
  struct Context {
    S *s;
    static bool save(void *ptr, ZuBSpan data) {
      *static_cast<Context *>(ptr)->s << data;
      return true;
    }
  } context{&s};
  if (!Backend::pkey_csr(key, &context, Context::save))
    return ZeEXCEPT(Error, "ZtlsPK", "CSR generation failed");
  return {};
}

// any key
struct AnyKey : public ZmPolymorph { };

// any backend key
struct AnyPK : public AnyKey {
  Backend::PKey	*key = nullptr;

  AnyPK() { key = Backend::pkey_new(); }

  ~AnyPK() { Backend::pkey_free(key); }
};

// RSA public key
template <typename Heap>
struct PK_RSA_ : public Heap, public AnyPK {
  enum { Secret = 1 };

protected:
  PK_RSA_() { }

public:
  // load from PKCS#1 public key data
  PK_RSA_(const Data::PK_PKCS1 &data) {
    if (!Backend::pkey_rsa_import_public(
	key, data.modulus, data.pubExp)) {
      throw ZeEXCEPT(Error, "ZtlsPK", "RSA public key import failed");
    }
  }

  // save public key
  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const { return save_PK_RSA(s, key); }

  // verify signature
  template <MDType MDType = SHA256>
  ZuUnion<bool, ZeException> verify(ZuBSpan data, ZuBSpan signature) {
    if (signature.length() != Backend::pkey_rsa_size(key))
      return ZeEXCEPT(Error, "ZtlsPK", "invalid RSA signature");
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    return Backend::pkey_verify(key, MDType, data, signature);
  }
};
using PK_RSA_Heap = ZmHeap<"Ztls.PK_RSA", PK_RSA_<ZuVoid>>;
ZuDerive(PK_RSA, (PK_RSA_<PK_RSA_Heap>));

// RSA private key
template <typename Heap>
struct SK_RSA_ : public PK_RSA_<Heap> {
  enum { Secret = 1 };
  using PK = PK_RSA;
  using PK_ = PK_RSA_<Heap>;
  using PK_::key;

  // generate key
  SK_RSA_(Random &rng, unsigned bits) {
    (void)rng;
    if (!Backend::pkey_rsa_generate(key, bits))
      throw ZeEXCEPT(Error, "ZtlsPK", "RSA key generation failed");
  }

  // load key from PKCS#1 data
  SK_RSA_(const Data::SK_PKCS1 &data) {
    if (!Backend::pkey_rsa_import_private(
	key, data.modulus, data.pubExp, data.prvExp,
	data.prime1, data.prime2, data.exp1, data.exp2, data.coeff))
      throw ZeEXCEPT(Error, "ZtlsPK", "RSA private key import failed");
  }

  // save private key
  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const {
    using namespace Save_;

    unsigned n = Backend::pkey_rsa_size(key);

    auto modulus = ZmScratch(uint8_t, n);
    auto pubExp = ZmScratch(uint8_t, 8);
    auto prvExp = ZmScratch(uint8_t, n);
    unsigned p = n>>1;
    auto prime1 = ZmScratch(uint8_t, p);
    auto prime2 = ZmScratch(uint8_t, p);
    auto exp1 = ZmScratch(uint8_t, p);
    auto exp2 = ZmScratch(uint8_t, p);
    auto coeff = ZmScratch(uint8_t, p);
    modulus.length(n);
    pubExp.length(8);
    prvExp.length(n);
    prime1.length(p);
    prime2.length(p);
    exp1.length(p);
    exp2.length(p);
    coeff.length(p);

    if (!Backend::pkey_rsa_export_private(
	key, modulus, pubExp, prvExp,
	prime1, prime2, exp1, exp2, coeff))
      return ZeEXCEPT(Error, "ZtlsPK", "RSA private key export failed");

    Data::SK_PKCS8_RSA data{
      .version = 0,
      .id = OIDs::PKCS1_RSA,
      .rsa = {	// PKCS#1
	0,
	modulus,
	pubExp, prvExp,
	prime1, prime2,
	exp1, exp2,
	coeff
      }
    };

    ZfASN1::save(s, data);

    return {};
  }

  // save public key
  template <typename S>
  ZuUnion<void, ZeException> savePK(S &s) const { return PK_::save(s); }

  template <typename S>
  ZuUnion<void, ZeException> saveCSR(S &s) const { return saveCSR_(s, key); }

  // create public key
  ZuUnion<ZmRef<PK>, ZeException> mkPK() {
    using namespace Data;

    auto buf = ZmScratch(char, DERBufSize);
    auto r = savePK(buf);
    if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
    auto data = ZfASN1::handler<PK_X509_RSA>(buf).ctor();
    try {
      return ZmRef(new PK{data.rsa});
    } catch (const ZeException &e) {
      return e;
    }
  }

  // sign data
  template <MDType MDType = SHA256, typename L>
  ZuUnion<void, ZeException> sign(Random &rng, ZuBSpan data, L &&l) {
    (void)rng;
    unsigned n = Backend::pkey_rsa_size(key);
    auto signature = ZmScratch(uint8_t, n);
    signature.length(n);
    size_t k = 0;
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    if (!Backend::pkey_sign(key, MDType, data, signature, &k))
      return ZeEXCEPT(Error, "ZtlsPK", "RSA sign failed");
    signature.length(k);
    ZuFwd<L>(l)(signature);
    return {};
  }
};
using SK_RSA_Heap = ZmHeap<"Ztls.SK_RSA", SK_RSA_<ZuVoid>>;
ZuDerive(SK_RSA, (SK_RSA_<SK_RSA_Heap>));

// EC public key
template <typename Heap>
struct PK_EC_ : public Heap, public AnyPK {
  enum { Secret = 0 };

protected:
  PK_EC_() { }

public:
  // load key from X509 data
  PK_EC_(ZuBSpan id_, ZuBSpan key) {
    if (!Backend::pkey_ec_import_public(this->key, id_, key))
      throw ZeEXCEPT(Error, "ZtlsPK", "EC public key import failed");
  }

  // save public key
  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const {
    return save_PK_EC(s, key);
  }

  // verify signature
  template <MDType MDType = SHA256>
  ZuUnion<bool, ZeException> verify(ZuBSpan data, ZuBSpan signature) {
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    if (!Backend::pkey_verify(key, MDType, data, signature))
      return ZeEXCEPT(Error, "ZtlsPK", "EC verify failed");
    return true;
  }
};
using PK_EC_Heap = ZmHeap<"Ztls.PK_EC", PK_EC_<ZuVoid>>;
ZuDerive(PK_EC, (PK_EC_<PK_EC_Heap>));

// EC private key
template <typename Heap>
struct SK_EC_ : public PK_EC_<Heap> {
  enum { Secret = 1 };
  using PK = PK_EC;
  using PK_ = PK_EC_<Heap>;
  using PK_::key;

  // generate key
  SK_EC_(Random &rng, ZuBSpan id_) {
    (void)rng;
    if (!Backend::pkey_ec_generate(key, id_))
      throw ZeEXCEPT(Error, "ZtlsPK", "EC key generation failed");
  }

  // load key from SEC1 / PKCS#8 data
  SK_EC_(Random &rng, ZuBSpan id_, ZuBSpan key) {
    (void)rng;
    if (!Backend::pkey_ec_import_private(this->key, id_, key))
      throw ZeEXCEPT(Error, "ZtlsPK", "EC private key import failed");
  }

  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const {
    using namespace Save_;

    unsigned oidLen = Backend::pkey_ec_oid_size(key);
    auto oid = ZmScratch(uint8_t, oidLen);
    oid.length(oidLen);
    if (!Backend::pkey_ec_export_oid(key, oid))
      return ZeEXCEPT(Error, "ZtlsPK", "EC OID export failed");

    unsigned n = Backend::pkey_ec_key_size(key);
    auto prvKey = ZmScratch(uint8_t, n);
    prvKey.length(n);
    if (!Backend::pkey_ec_export_private(this->key, prvKey))
      return ZeEXCEPT(Error, "ZtlsPK", "EC private key export failed");

    unsigned pubLen = Backend::pkey_ec_public_size(this->key);
    auto pubKey = ZmScratch(uint8_t, pubLen);
    pubKey.length(pubLen);
    if (!Backend::pkey_ec_export_public(this->key, pubKey))
      return ZeEXCEPT(Error, "ZtlsPK", "EC public key export failed");

    Data::SK_PKCS8_EC data{
      .version = 0,
      .id = OIDs::EC_ALG_UNRESTRICTED,
      .id2 = oid,
      .ec = {	// SEC1
	.version = 1,
	.key = prvKey,
	.id = {},		// ID (unused when embedded in PKCS#8 wrapper)
	.pubKey = pubKey	// optional public key
      }
    };

    ZfASN1::save(s, data);

    return {};
  }

  // save public key
  template <typename S>
  ZuUnion<void, ZeException> savePK(S &s) const { return PK_::save(s); }

  template <typename S>
  ZuUnion<void, ZeException> saveCSR(S &s) const { return saveCSR_(s, key); }

  // create public key
  ZuUnion<ZmRef<PK>, ZeException> mkPK() {
    using namespace Data;

    auto buf = ZmScratch(char, DERBufSize);
    auto r = savePK(buf);
    if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
    auto data = ZfASN1::handler<PK_X509_EC>(buf).ctor();
    try {
      return ZmRef(new PK{data.id2, data.pubKey});
    } catch (const ZeException &e) {
      return e;
    }
  }

  // sign data
  template <MDType MDType = SHA256, typename L>
  ZuUnion<void, ZeException> sign(Random &rng, ZuBSpan data, L &&l) {
    (void)rng;
    unsigned n = Backend::pkey_ec_key_size(key);
    n += ZfASN1::len_uint(n) + 2;	// ASN.1 Integer
    n = (n<<1);				// x2
    n += ZfASN1::len_uint(n) + 1;	// ASN.1 Sequence
    auto signature = ZmScratch(uint8_t, n);
    signature.length(n);
    size_t k = 0;
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    if (!Backend::pkey_sign(key, MDType, data, signature, &k))
      return ZeEXCEPT(Error, "ZtlsPK", "EC sign failed");
    signature.length(k);
    ZuFwd<L>(l)(signature);
    return {};
  }
};
using SK_EC_Heap = ZmHeap<"Ztls.SK_EC", SK_EC_<ZuVoid>>;
ZuDerive(SK_EC, (SK_EC_<SK_EC_Heap>));

// ED25519 public key
template <typename Heap>
struct PK_ED25519_ : public Heap, public AnyPK {
  enum { Secret = 0 };

protected:
  PK_ED25519_() { }

public:
  // load key from X509 data
  PK_ED25519_(ZuBSpan key) {
    if (!Backend::pkey_ed25519_import_public(this->key, key))
      throw ZeEXCEPT(Error, "ZtlsPK", "ED25519 public key import failed");
  }

  // save public key
  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const {
    uint8_t pubKey[32];
    if (!Backend::pkey_ed25519_export_public(this->key, pubKey))
      return ZeEXCEPT(Error, "ZtlsPK", "ED25519 public key export failed");

    Data::PK_X509_ED25519 data{
      .id = OIDs::ED25519,
      .pubKey = ZuBSpan{pubKey, sizeof(pubKey)}
    };

    ZfASN1::save(s, data);

    return {};
  }
  
  // verify signature
  template <MDType MDType = SHA256>
  ZuUnion<bool, ZeException> verify(ZuBSpan data, ZuBSpan signature) {
    if (signature.length() != 64)
      return ZeEXCEPT(Error, "ZtlsPK", "invalid ED25519 signature");
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    return Backend::pkey_verify(this->key, MDType, data, signature);
  }
};
using PK_ED25519_Heap = ZmHeap<"Ztls.PK_ED25519", PK_ED25519_<ZuVoid>>;
ZuDerive(PK_ED25519, (PK_ED25519_<PK_ED25519_Heap>));

// ED25519 private key
template <typename Heap>
struct SK_ED25519_ : public PK_ED25519_<Heap> {
  enum { Secret = 1 };
  using PK = PK_ED25519;
  using PK_ = PK_ED25519_<Heap>;
  using PK_::key;

  // generate key
  SK_ED25519_(Random &rng) {
    (void)rng;
    if (!Backend::pkey_ed25519_generate(key))
      throw ZeEXCEPT(Error, "ZtlsPK", "ED25519 key generation failed");
  }

  // load key from PKCS#8 data
  SK_ED25519_(ZuBSpan key) {
    if (!Backend::pkey_ed25519_import_private(this->key, key))
      throw ZeEXCEPT(Error, "ZtlsPK", "ED25519 private key import failed");
  }

  // save private key
  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const {
    uint8_t prvKey[32];
    if (!Backend::pkey_ed25519_export_private(key, prvKey))
      return ZeEXCEPT(Error, "ZtlsPK", "ED25519 private key export failed");

    Data::SK_PKCS8_ED25519 data{
      .version = 0,
      .id = OIDs::ED25519,
      .key = ZuBSpan{prvKey, sizeof(prvKey)}
    };

    ZfASN1::save(s, data);

    return {};
  }
  
  // save public key
  template <typename S>
  ZuUnion<void, ZeException> savePK(S &s) const { return PK_::save(s); }

  template <typename S>
  ZuUnion<void, ZeException> saveCSR(S &s) const { return saveCSR_(s, key); }

  // create public key
  ZuUnion<ZmRef<PK>, ZeException> mkPK() {
    using namespace Data;

    auto buf = ZmScratch(char, DERBufSize);
    auto r = savePK(buf);
    if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
    auto data = ZfASN1::handler<PK_X509_ED25519>(buf).ctor();
    try {
      return ZmRef(new PK{data.pubKey});
    } catch (const ZeException &e) {
      return e;
    }
  }

  // sign data
  template <MDType MDType = SHA256, typename L>
  ZuUnion<void, ZeException> sign(Random &, ZuBSpan data, L &&l) {
    uint8_t signature[64];
    size_t k = 0;
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    if (!Backend::pkey_sign(
	key, MDType, data, {signature, sizeof(signature)}, &k) ||
	k != sizeof(signature))
      return ZeEXCEPT(Error, "ZtlsPK", "ED25519 sign failed");
    ZuFwd<L>(l)(ZuBSpan{signature, sizeof(signature)});
    return {};
  }
};
using SK_ED25519_Heap = ZmHeap<"Ztls.SK_ED25519", SK_ED25519_<ZuVoid>>;
ZuDerive(SK_ED25519, (SK_ED25519_<SK_ED25519_Heap>));

template <typename Impl>
struct Load {
  ZuInline Impl *impl() { return static_cast<Impl *>(this); }
  ZuInline const Impl *impl() const { return static_cast<const Impl *>(this); }

  ZuUnion<ZmRef<AnyKey>, ZeException>
  loadB64(ZuSpan<uint8_t> span, int hint) {
    // in-place-overwrite decoded
    span.trunc(ZuBase64::decode(
	{&span[0], ZuBase64::declen(span.length())}, span));
    return impl()->load(span, hint);
  }

  ZuInline static constexpr bool isspace__(char c) {
    return ((c >= '\t' && c <= '\r') || c == ' ');
  }
  ZuUnion<ZmRef<AnyKey>, ZeException> loadPEM(ZuSpan<char> span) {
    using namespace Data;

    {
      // there can be leading and trailing junk in a PEM file...
      // find the header and offset the span
      unsigned offset, n;
      int type = pemHead(span, offset, n);
      if (type < 0) goto bad;
      // ensure the span has enough space for a header and trailer
      if (span.length() < offset + ((n<<1) - 2)) goto bad;
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
	if (isspace__(span[i])) continue;
	if (ZuLikely(o == i)) { o++; continue; }
	span[o++] = span[i];
      }
      span.trunc(o);
      return loadB64(span, type);
    }
  bad:
    return ZeEXCEPT(Error, "ZtlsPK", "invalid PEM format");
  }

  template <typename Path>
  ZuUnion<ZmRef<AnyKey>, ZeException> loadFile(const Path &path) {
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
  static int id(ZuSpan<uint8_t> span) {
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
      case Sequence:	return Type::PK_X509;
      case Integer:	return Type::PK_PKCS1;
    }
    return -1;
  }

public:
  ZuUnion<ZmRef<AnyKey>, ZeException>
  load(ZuSpan<char> span, int hint = -1) {
    using namespace Data;

    int type = id(span);
    if (type < 0)
      return ZeEXCEPT(Error, "ZtlsPK", "invalid key data");
    if (hint >= 0 && type != hint)
      return ZeEXCEPT(Error, "ZtlsPK", "inconsistent key data");
    ZmRef<AnyKey> key;
    try {
      switch (type) {
	case Type::PK_X509: {
	  auto hdr = ZfASN1::handler<PK_X509_HDR>(span).ctor();
	  auto id = Load_::keyType(hdr.id);
	  if (id < 0) return ZeEXCEPT(Error, "ZtlsPK", ([id = ZtBArray(hdr.id)](auto &s) {
	    ZmHex::enc(id, [&s](ZuCSpan id) {
	      s << "unknown X509 OID " << id;
	    });
	  }));
	  switch (id) {
	    case 0: {
	      auto data = ZfASN1::handler<PK_X509_RSA>(span).ctor();
	      key = new PK_RSA(data.rsa);
	    } break;
	    case 1: {
	      auto data = ZfASN1::handler<PK_X509_EC>(span).ctor();
	      key = new PK_EC(data.id2, data.pubKey);
	    } break;
	    case 2: {
	      auto data = ZfASN1::handler<PK_X509_ED25519>(span).ctor();
	      key = new PK_ED25519(data.pubKey);
	    } break;
	  }
	} break;
	case Type::PK_PKCS1: {
	  auto data = ZfASN1::handler<PK_PKCS1>(span).ctor();
	  key = new PK_RSA(data);
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
  static int id(ZuSpan<uint8_t> span) {
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
      case Sequence:	return Type::SK_PKCS8;
      case OctetString:	return Type::SK_SEC1;
      case Integer:	return Type::SK_PKCS1;
    }
    return -1;
  }

public:
  ZuUnion<ZmRef<AnyKey>, ZeException>
  load(ZuSpan<char> span, int hint = -1) {
    using namespace Data;

    int type = id(span);
    if (type < 0)
      return ZeEXCEPT(Error, "ZtlsPK", "invalid key data");
    if (hint >= 0 && type != hint)
      return ZeEXCEPT(Error, "ZtlsPK", "inconsistent key data");
    ZmRef<AnyKey> key;
    try {
      switch (type) {
	case Type::SK_PKCS8: {
	  auto hdr = ZfASN1::handler<SK_PKCS8_HDR>(span).ctor();
	  auto id = Load_::keyType(hdr.id);
	  if (id < 0) return ZeEXCEPT(Error, "ZtlsPK", ([id = ZtBArray(hdr.id)](auto &s) {
	    ZmHex::enc(id, [&s](ZuCSpan id) {
	      s << "unknown PKCS#8 OID " << id;
	    });
	  }));
	  switch (id) {
	    case 0: {
	      auto data = ZfASN1::handler<SK_PKCS8_RSA>(span).ctor();
	      key = new SK_RSA(data.rsa);
	    } break;
	    case 1: {
	      auto data = ZfASN1::handler<SK_PKCS8_EC>(span).ctor();
	      key = new SK_EC(m_rng, data.id2, data.ec.key);
	    } break;
	    case 2: {
	      auto data = ZfASN1::handler<SK_PKCS8_ED25519>(span).ctor();
	      key = new SK_ED25519(data.key);
	    } break;
	  }
	} break;
	case Type::SK_SEC1: {
	  auto data = ZfASN1::handler<SK_SEC1>(span).ctor();
	  key = new SK_EC(m_rng, data.id, data.key);
	}
	case Type::SK_PKCS1: {
	  auto data = ZfASN1::handler<SK_PKCS1>(span).ctor();
	  key = new SK_RSA(data);
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

template <typename S, typename Key>
ZuUnion<void, ZeException> saveB64(S &s, Key *key) {
  using namespace Data;

  auto buf = ZmScratch(char, DERBufSize);
  auto r = key->save(buf);
  if (ZuUnlikely(r.template is<ZeException>())) return r;
  ZmBase64::enc(buf, [&s](ZuCSpan b64) { s << b64; });
  return {};
}

template <typename S, typename Key>
ZuUnion<void, ZeException> savePEM(S &s, const Key *key) {
  using namespace Data;

  auto buf = ZmScratch(char, BufSize);
  auto r = key->save(buf);
  if (ZuUnlikely(r.template is<ZeException>())) return r;
  ZmBase64::enc(buf, [&s](ZuCSpan b64) {
    if constexpr (Key::Secret)
      s << "-----BEGIN PRIVATE KEY-----\n";
    else
      s << "-----BEGIN PUBLIC KEY-----\n";
    unsigned n = b64.length();
    for (unsigned i = 0; i < n; ) {
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
ZuUnion<void, ZeException> saveFile(const Path &path, const Key *key) {
  using namespace Data;

  ZiFile file;
  if (file.open(path, ZiFile::Write) != Zi::OK)
    return ZeEXCEPT(Error, "ZtlsPK", ([path, e = file.error()](auto &s) {
      s << "\"" << path << "\" - open failed: " << e;
    }));
  auto buf = ZmScratch(char, BufSize);
  auto r = savePEM(buf, key);
  if (ZuUnlikely(r.template is<ZeException>())) return r;
  if (file.write(buf.data(), buf.length()) < buf.length())
    return ZeEXCEPT(Error, "ZtlsPK", ([path, e = file.error()](auto &s) {
      s << "\"" << path << "\" - write failed: " << e;
    }));
  file.close();
  return {};
}

} // Ztls::PK

#endif /* ZtlsPK_HH */
