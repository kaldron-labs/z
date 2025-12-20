//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// mbedtls C++ wrapper - public/private keys
// - private key generate/save/load/sign
// - public key save/load/verify
// - reads any of PKCS#8, PKCS#1, SEC1, X509
// - writes PKCS#8 for private keys, X509 for public keys
// - DER, b64 and PEM support
// - extended beyond mbedtls for ED25519 keys
// - SK* - private (secret) key related
// - PK* - public key related

#ifndef ZtlsPK_HH
#define ZtlsPK_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZmCodec.hh>

#include <zlib/ZtASN1.hh>

#include <zlib/ZeLog.hh>

#include <zlib/ZiFile.hh>

#include <mbedtls/platform.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/oid.h>
#include <mbedtls/bignum.h>
#include <mbedtls/pk.h>

#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPK_Data.hh>
#include <zlib/Ztls_ed25519.h>

namespace Ztls::PK {

namespace Save_ {

inline auto mwb_error_(int e) {
  return [e](auto &s) {
    s << "mbedtls_mpi_write_binary failed: " << strerror_(e);
  };
}
// use a macro to get the right line number in the event
#define ZtlsPK_mwb_error(e) ZeEXCEPT(Error, mwb_error_(e))

} // Save_

namespace Load_ {

constexpr auto matcher = ZuMatcher<
  MBEDTLS_OID_PKCS1_RSA,		// RSA
  MBEDTLS_OID_EC_ALG_UNRESTRICTED,	// EC
  MBEDTLS_OID_ED25519			// ED25519
>();

inline auto mrb_error_(int e) {
  return [e](auto &s) {
    s << "mbedtls_mpi_read_binary failed: " << strerror_(e);
  };
}
// use a macro to get the right line number in the event
#define ZtlsPK_mrb_error(e) ZeEXCEPT(Error, mrb_error_(e))
} // Load_

// save RSA public key as X.509
template <typename S>
inline ZuUnion<void, ZeException>
save_PK_RSA(S &s, const mbedtls_pk_context &ctx) {
  using namespace Save_;

  auto rsa = mbedtls_pk_rsa(ctx);
  unsigned n = mbedtls_rsa_get_len(rsa);

  auto modulus_ = ZmAlloc(uint8_t, n);
  auto pubExp_ = ZmAlloc(uint8_t, 8);

  ZuSpan<uint8_t> modulus(&modulus_[0], n);
  ZuSpan<uint8_t> pubExp(&pubExp_[0], 8);

  {
    int e;
    if (e = mbedtls_mpi_write_binary(&rsa->private_N, &modulus[0], n))
      return ZtlsPK_mwb_error(e);
    if (e = mbedtls_mpi_write_binary(&rsa->private_E, &pubExp[0], 8))
      return ZtlsPK_mwb_error(e);
  }

  Data::PK_X509_RSA data{
    .id = MBEDTLS_OID_PKCS1_RSA,
    .rsa = {	// PKCS#1
      modulus,
      pubExp
    }
  };

  ZtASN1::save(s, data);

  return {};
}

// save EC public key as X.509
template <typename S>
inline ZuUnion<void, ZeException>
save_PK_EC(S &s, const mbedtls_pk_context &ctx) {
  using namespace Save_;

  auto ec = mbedtls_pk_ec(ctx);

  // extract OID
  const char *oid;
  size_t olen;
  mbedtls_oid_get_oid_by_ec_grp(ec->private_grp.id, &oid, &olen);

  // extract public key
  unsigned n = (ec->private_grp.nbits + 7)>>3;
  auto pubKey_ = ZmAlloc(uint8_t, (n<<1) + 1);
  ZuSpan<uint8_t> pubKey(&pubKey_[0], (n<<1) + 1);
  pubKey[0] = 0x04;
  if (int e = mbedtls_mpi_write_binary(
      &ec->private_Q.private_X, &pubKey[1], n))
    return ZtlsPK_mwb_error(e);
  if (int e = mbedtls_mpi_write_binary(
      &ec->private_Q.private_Y, &pubKey[n + 1], n))
    return ZtlsPK_mwb_error(e);

  Data::PK_X509_EC data{
    .id = MBEDTLS_OID_EC_ALG_UNRESTRICTED,
    .id2 = {oid, unsigned(olen)},
    .pubKey = pubKey,
  };

  ZtASN1::save(s, data);

  return {};
}

// any key
struct AnyKey : public ZmPolymorph { };

// any mbedtls_pk key
struct AnyPK : public AnyKey {
  mbedtls_pk_context	ctx;

  AnyPK() { mbedtls_pk_init(&ctx); }

  ~AnyPK() { mbedtls_pk_free(&ctx); }
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
    using namespace Load_;

    mbedtls_pk_setup(&ctx, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    auto rsa = mbedtls_pk_rsa(ctx);
    if (int e = mbedtls_rsa_import_raw(rsa,
	&data.modulus[0], data.modulus.length(),
	nullptr, 0,
	nullptr, 0,
	nullptr, 0,
	&data.pubExp[0], data.pubExp.length()))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_rsa_import_raw failed: " << strerror_(e);
      }));
    if (int e = mbedtls_rsa_complete(rsa))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_rsa_complete failed: " << strerror_(e);
      }));
  }

  // save public key
  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const { return save_PK_RSA(s, ctx); }

  // verify signature
  template <mbedtls_md_type_t MDType = MBEDTLS_MD_SHA256>
  ZuUnion<bool, ZeException> verify(ZuBSpan data, ZuBSpan signature) {
    auto rsa = mbedtls_pk_rsa(ctx);
    if (signature.length() != mbedtls_rsa_get_len(rsa))
      return ZeEXCEPT(Error, "invalid RSA signature");
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    return !mbedtls_pk_verify(
      &ctx, MDType, &data[0], data.length(),
      &signature[0], signature.length());
  }
};
using PK_RSA_Heap = ZmHeap<"Ztls.PK_RSA", PK_RSA_<ZuEmpty>>;
ZuDerive(PK_RSA, (PK_RSA_<PK_RSA_Heap>));

// RSA private key
template <typename Heap>
struct SK_RSA_ : public PK_RSA_<Heap> {
  enum { Secret = 1 };
  using PK = PK_RSA;
  using PK_ = PK_RSA_<Heap>;
  using PK_::ctx;

  // generate key
  SK_RSA_(Random &rng, unsigned bits) {
    mbedtls_pk_setup(&ctx, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    auto rsa = mbedtls_pk_rsa(ctx);
    mbedtls_rsa_gen_key(rsa,
      mbedtls_ctr_drbg_random,
      rng.ctr_drbg(), bits, 65537);
  }

  // load key from PKCS#1 data
  SK_RSA_(const Data::SK_PKCS1 &data) {
    using namespace Load_;

    mbedtls_pk_setup(&ctx, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    auto rsa = mbedtls_pk_rsa(ctx);
    if (int e = mbedtls_rsa_import_raw(rsa,
	&data.modulus[0], data.modulus.length(),
	&data.prime1[0], data.prime1.length(),
	&data.prime2[0], data.prime2.length(),
	&data.prvExp[0], data.prvExp.length(),
	&data.pubExp[0], data.pubExp.length()))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_rsa_import_raw failed: " << strerror_(e);
      }));
    if (int e = mbedtls_rsa_complete(rsa))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_rsa_complete failed: " << strerror_(e);
      }));
  }

  // save private key
  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const {
    using namespace Save_;

    auto rsa = mbedtls_pk_rsa(ctx);
    unsigned n = mbedtls_rsa_get_len(rsa);

    auto modulus_ = ZmAlloc(uint8_t, n);
    auto pubExp_ = ZmAlloc(uint8_t, 8);
    auto prvExp_ = ZmAlloc(uint8_t, n);
    auto prime1_ = ZmAlloc(uint8_t, (n>>1));
    auto prime2_ = ZmAlloc(uint8_t, (n>>1));
    auto exp1_ = ZmAlloc(uint8_t, (n>>1));
    auto exp2_ = ZmAlloc(uint8_t, (n>>1));
    auto coeff_ = ZmAlloc(uint8_t, (n>>1));

    ZuSpan<uint8_t> modulus(&modulus_[0], n);
    ZuSpan<uint8_t> pubExp(&pubExp_[0], 8);
    ZuSpan<uint8_t> prvExp(&prvExp_[0], n);
    ZuSpan<uint8_t> prime1(&prime1_[0], (n>>1));
    ZuSpan<uint8_t> prime2(&prime2_[0], (n>>1));
    ZuSpan<uint8_t> exp1(&exp1_[0], (n>>1));
    ZuSpan<uint8_t> exp2(&exp2_[0], (n>>1));
    ZuSpan<uint8_t> coeff(&coeff_[0], (n>>1));

    {
      int e;
      if (e = mbedtls_mpi_write_binary(&rsa->private_N, &modulus[0], n))
	return ZtlsPK_mwb_error(e);
      if (e = mbedtls_mpi_write_binary(&rsa->private_E, &pubExp[0], 8))
	return ZtlsPK_mwb_error(e);
      if (e = mbedtls_mpi_write_binary(&rsa->private_D, &prvExp[0], n))
	return ZtlsPK_mwb_error(e);
      if (e = mbedtls_mpi_write_binary(&rsa->private_P, &prime1[0], n>>1))
	return ZtlsPK_mwb_error(e);
      if (e = mbedtls_mpi_write_binary(&rsa->private_Q, &prime2[0], n>>1))
	return ZtlsPK_mwb_error(e);
      if (e = mbedtls_mpi_write_binary(&rsa->private_DP, &exp1[0], n>>1))
	return ZtlsPK_mwb_error(e);
      if (e = mbedtls_mpi_write_binary(&rsa->private_DQ, &exp2[0], n>>1))
	return ZtlsPK_mwb_error(e);
      if (e = mbedtls_mpi_write_binary(&rsa->private_QP, &coeff[0], n>>1))
	return ZtlsPK_mwb_error(e);
    }

    Data::SK_PKCS8_RSA data{
      .version = 0,
      .id = MBEDTLS_OID_PKCS1_RSA,
      .rsa = {	// PKCS#1
	0,
	modulus,
	pubExp, prvExp,
	prime1, prime2,
	exp1, exp2,
	coeff
      }
    };

    ZtASN1::save(s, data);

    return {};
  }

  // save public key
  template <typename S>
  ZuUnion<void, ZeException> savePK(S &s) const { return PK_::save(s); }

  // create public key
  ZuUnion<ZmRef<PK>, ZeException> mkPK() {
    using namespace Data;

    auto buf_ = ZmAlloc(char, DERBufSize);
    ZtArray<char> buf(&buf_[0], 0, DERBufSize, false);
    auto r = savePK(buf);
    if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
    auto data = ZtASN1::handler<PK_X509_RSA>(buf).ctor();
    try {
      return ZmMkRef(new PK{data.rsa});
    } catch (const ZeException &e) {
      return e;
    }
  }

  // sign data
  template <mbedtls_md_type_t MDType = MBEDTLS_MD_SHA256, typename L>
  ZuUnion<void, ZeException> sign(Random &rng, ZuBSpan data, L &&l) {
    auto rsa = mbedtls_pk_rsa(ctx);
    unsigned n = mbedtls_rsa_get_len(rsa);
    auto signature = ZmAlloc(uint8_t, n);
    size_t k;
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    if (int e = mbedtls_pk_sign(
	&ctx, MDType, &data[0], data.length(),
	&signature[0], n, &k,
	mbedtls_ctr_drbg_random, rng.ctr_drbg()))
      return ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_pk_sign failed: " << strerror_(e);
      }));
    ZuFwd<L>(l)(ZuBSpan{&signature[0], k});
    return {};
  }
};
using SK_RSA_Heap = ZmHeap<"Ztls.SK_RSA", SK_RSA_<ZuEmpty>>;
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
    using namespace Load_;

    mbedtls_pk_setup(&ctx, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    auto ec = mbedtls_pk_ec(ctx);

    mbedtls_asn1_buf oid{
      ZtASN1::OID, id_.length(), const_cast<uint8_t *>(&id_[0])};
    mbedtls_ecp_group_id id;
    if (int e = mbedtls_oid_get_ec_grp(&oid, &id))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_oid_get_ec_grp failed: " << strerror_(e);
      }));

    if (int e = mbedtls_ecp_group_load(&ec->private_grp, id))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_ecp_group_load failed: " << strerror_(e);
      }));
    if (int e = mbedtls_ecp_point_read_binary(
	&ec->private_grp, &ec->private_Q, &key[0], key.length()))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_ecp_point_read_binary failed: " << strerror_(e);
      }));
  }

  // save public key
  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const {
    return save_PK_EC(s, ctx);
  }

  // verify signature
  template <mbedtls_md_type_t MDType = MBEDTLS_MD_SHA256>
  ZuUnion<bool, ZeException> verify(ZuBSpan data, ZuBSpan signature) {
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    if (int e = mbedtls_pk_verify(
	&ctx, MDType, &data[0], data.length(),
	&signature[0], signature.length())) {
      return ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_pk_verify failed: " << strerror_(e);
      }));
    }
    return true;
  }
};
using PK_EC_Heap = ZmHeap<"Ztls.PK_EC", PK_EC_<ZuEmpty>>;
ZuDerive(PK_EC, (PK_EC_<PK_EC_Heap>));

// EC private key
template <typename Heap>
struct SK_EC_ : public PK_EC_<Heap> {
  enum { Secret = 1 };
  using PK = PK_EC;
  using PK_ = PK_EC_<Heap>;
  using PK_::ctx;

  // generate key
  SK_EC_(Random &rng, ZuBSpan id_) {
    mbedtls_pk_setup(&ctx, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    auto ec = mbedtls_pk_ec(ctx);

    mbedtls_asn1_buf oid{
      ZtASN1::OID, id_.length(), const_cast<uint8_t *>(&id_[0])};
    mbedtls_ecp_group_id id;
    if (int e = mbedtls_oid_get_ec_grp(&oid, &id))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_oid_get_ec_grp failed: " << strerror_(e);
      }));

    if (int e = mbedtls_ecp_gen_key(
	id, ec, mbedtls_ctr_drbg_random, rng.ctr_drbg()))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_ecp_gen_key failed: " << strerror_(e);
      }));
  }

  // load key from SEC1 / PKCS#8 data
  SK_EC_(Random &rng, ZuBSpan id_, ZuBSpan key) {
    using namespace Load_;

    mbedtls_pk_setup(&ctx, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    auto ec = mbedtls_pk_ec(ctx);

    mbedtls_asn1_buf oid{
      ZtASN1::OID, id_.length(), const_cast<uint8_t *>(&id_[0])};
    mbedtls_ecp_group_id id;
    if (int e = mbedtls_oid_get_ec_grp(&oid, &id))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_oid_get_ec_grp failed: " << strerror_(e);
      }));

    if (int e = mbedtls_ecp_group_load(&ec->private_grp, id))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_ecp_group_load failed: " << strerror_(e);
      }));
    if (int e = mbedtls_mpi_read_binary(
	&ec->private_d, &key[0], key.length()))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_mpi_read_binary failed: " << strerror_(e);
      }));
    if (int e = mbedtls_ecp_mul(
	&ec->private_grp, &ec->private_Q, &ec->private_d, &ec->private_grp.G,
	mbedtls_ctr_drbg_random, rng.ctr_drbg()))
      throw ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_ecp_mul failed: " << strerror_(e);
      }));
  }

  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const {
    using namespace Save_;

    auto ec = mbedtls_pk_ec(ctx);

    // extract OID
    const char *oid;
    size_t olen;
    mbedtls_oid_get_oid_by_ec_grp(ec->private_grp.id, &oid, &olen);

    // extract private key
    unsigned n = (ec->private_grp.nbits + 7)>>3;
    auto key_ = ZmAlloc(uint8_t, n);
    ZuSpan<uint8_t> key(&key_[0], n);
    if (int e = mbedtls_mpi_write_binary(&ec->private_d, &key[0], n))
      return ZtlsPK_mwb_error(e);

    // extract public key
    auto pubKey_ = ZmAlloc(uint8_t, (n<<1) + 1);
    ZuSpan<uint8_t> pubKey(&pubKey_[0], (n<<1) + 1);
    pubKey[0] = 0x04;
    if (int e = mbedtls_mpi_write_binary(
	&ec->private_Q.private_X, &pubKey[1], n))
      return ZtlsPK_mwb_error(e);
    if (int e = mbedtls_mpi_write_binary(
	&ec->private_Q.private_Y, &pubKey[n + 1], n))
      return ZtlsPK_mwb_error(e);

    Data::SK_PKCS8_EC data{
      .version = 0,
      .id = MBEDTLS_OID_EC_ALG_UNRESTRICTED,
      .id2 = {oid, unsigned(olen)},
      .ec = {	// SEC1
	.version = 1,
	.key = key,
	.id = {},		// ID (unused when embedded in PKCS#8 wrapper)
	.pubKey = pubKey	// optional public key
      }
    };

    ZtASN1::save(s, data);

    return {};
  }

  // save public key
  template <typename S>
  ZuUnion<void, ZeException> savePK(S &s) const { return PK_::save(s); }

  // create public key
  ZuUnion<ZmRef<PK>, ZeException> mkPK() {
    using namespace Data;

    auto buf_ = ZmAlloc(char, DERBufSize);
    ZtArray<char> buf(&buf_[0], 0, DERBufSize, false);
    auto r = savePK(buf);
    if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
    auto data = ZtASN1::handler<PK_X509_EC>(buf).ctor();
    try {
      return ZmMkRef(new PK{data.id2, data.pubKey});
    } catch (const ZeException &e) {
      return e;
    }
  }

  // sign data
  template <mbedtls_md_type_t MDType = MBEDTLS_MD_SHA256, typename L>
  ZuUnion<void, ZeException> sign(Random &rng, ZuBSpan data, L &&l) {
    auto ec = mbedtls_pk_ec(ctx);
    unsigned n = (ec->private_grp.nbits + 7)>>3;
    n += ZtASN1::len_uint(n) + 2;	// ASN.1 Integer
    n = (n<<1);				// x2
    n += ZtASN1::len_uint(n) + 1;	// ASN.1 Sequence
    auto signature = ZmAlloc(uint8_t, n);
    size_t k;
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    if (int e = mbedtls_pk_sign(
	&ctx, MDType, &data[0], data.length(),
	&signature[0], n, &k,
	mbedtls_ctr_drbg_random, rng.ctr_drbg()))
      return ZeEXCEPT(Error, ([e](auto &s) {
	s << "mbedtls_pk_sign failed: " << strerror_(e);
      }));
    ZuFwd<L>(l)(ZuBSpan{&signature[0], k});
    return {};
  }
};
using SK_EC_Heap = ZmHeap<"Ztls.SK_EC", SK_EC_<ZuEmpty>>;
ZuDerive(SK_EC, (SK_EC_<SK_EC_Heap>));

// ED25519 public key
template <typename Heap>
struct PK_ED25519_ : public Heap, public AnyKey {
  enum { Secret = 0 };

  ed25519_public_key	pk;

protected:
  PK_ED25519_() { }

public:
  // load key from X509 data
  PK_ED25519_(ZuBSpan key) {
    using namespace Load_;

    if (key.length() != 32)
      throw ZeEXCEPT(Error, "PK_ED25519 - invalid key data");
    memcpy(pk, &key[0], 32);
  }

  // save public key
  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const {
    Data::PK_X509_ED25519 data{
      .id = MBEDTLS_OID_ED25519,
      .pubKey = pk
    };

    ZtASN1::save(s, data);

    return {};
  }
  
  // verify signature
  template <mbedtls_md_type_t MDType = MBEDTLS_MD_SHA256>
  ZuUnion<bool, ZeException> verify(ZuBSpan data, ZuBSpan signature) {
    if (signature.length() != sizeof(ed25519_signature))
      return ZeEXCEPT(Error, "invalid ED25519 signature");
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    return !ed25519_sign_open(
      &data[0], data.length(),
      pk, *reinterpret_cast<const ed25519_signature *>(&signature[0]));
  }
};
using PK_ED25519_Heap = ZmHeap<"Ztls.PK_ED25519", PK_ED25519_<ZuEmpty>>;
ZuDerive(PK_ED25519, (PK_ED25519_<PK_ED25519_Heap>));

// ED25519 private key
template <typename Heap>
struct SK_ED25519_ : public PK_ED25519_<Heap> {
  enum { Secret = 1 };
  using PK = PK_ED25519;
  using PK_ = PK_ED25519_<Heap>;
  using PK_::pk;

  ed25519_secret_key	sk;

  // generate key
  SK_ED25519_(Random &rng) {
    rng.random(sk);
    ed25519_publickey(sk, pk);
  }

  // load key from PKCS#8 data
  SK_ED25519_(ZuBSpan key) {
    using namespace Load_;

    if (key.length() != 32)
      throw ZeEXCEPT(Error, "SK_ED25519 - invalid key data");
    memcpy(sk, &key[0], 32);
    ed25519_publickey(sk, pk);
  }

  // save private key
  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const {
    Data::SK_PKCS8_ED25519 data{
      .version = 0,
      .id = MBEDTLS_OID_ED25519,
      .key = sk
    };

    ZtASN1::save(s, data);

    return {};
  }
  
  // save public key
  template <typename S>
  ZuUnion<void, ZeException> savePK(S &s) const { return PK_::save(s); }

  // create public key
  ZuUnion<ZmRef<PK>, ZeException> mkPK() {
    using namespace Data;

    auto buf_ = ZmAlloc(char, DERBufSize);
    ZtArray<char> buf(&buf_[0], 0, DERBufSize, false);
    auto r = savePK(buf);
    if (r.template is<ZeException>()) return ZuMv(r).template p<ZeException>();
    auto data = ZtASN1::handler<PK_X509_ED25519>(buf).ctor();
    try {
      return ZmMkRef(new PK{data.pubKey});
    } catch (const ZeException &e) {
      return e;
    }
  }

  // sign data
  template <mbedtls_md_type_t MDType = MBEDTLS_MD_SHA256, typename L>
  ZuUnion<void, ZeException> sign(Random &, ZuBSpan data, L &&l) {
    ed25519_signature signature;
    ZmAssert(data.length() == Ztls::MD<MDType>::Size);
    ed25519_sign(&data[0], data.length(), sk, pk, signature);
    ZuFwd<L>(l)(ZuBSpan{&signature[0], 64});
    return {};
  }
};
using SK_ED25519_Heap = ZmHeap<"Ztls.SK_ED25519", SK_ED25519_<ZuEmpty>>;
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
      auto [offset, type] = headMatcher.find(span);
      if (offset < 0) goto bad;
      // get the length of the matched header
      using HeadKeys = ZuDecay<decltype(headMatcher.keys())>;
      unsigned n = ZuSwitch::dispatch<HeadKeys::N>(type, [](auto I) {
	return ZuType<I, HeadKeys>{}().length();
      });
      // ensure the span has enough space for a header and trailer
      if (span.length() < offset + ((n<<1) - 2)) goto bad;
      span.offset(offset + n);
      // find the trailer and truncate the span
      {
	auto [offset_, type_] = tailMatcher.find(span);
	if (offset_ < 0 || type != type_) goto bad;
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
    return ZeEXCEPT(Error, "invalid PEM format");
  }

  template <typename Path>
  ZuUnion<ZmRef<AnyKey>, ZeException> loadFile(const Path &path) {
    using namespace Data;

    ZiFile file;
    if (file.open(path, ZiFile::ReadOnly, 0666) != Zi::OK)
      return ZeEXCEPT(Error, ([path, e = file.error()](auto &s) {
	s << "\"" << path << "\" open failed: " << e;
      }));
    auto size = file.size();
    if (size > BufSize)
      return ZeEXCEPT(Error, ([path, e = file.error()](auto &s) {
	s << "\"" << path << "\" read failed: file too large";
      }));
    auto buf_ = ZmAlloc(char, size);
    ZuSpan<char> buf(&buf_[0], unsigned(size));
    if (file.read(&buf[0], size) < size)
      return ZeEXCEPT(Error, ([path, e = file.error()](auto &s) {
	s << "\"" << path << "\" read failed: " << e;
      }));
    file.close();
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
    using namespace ZtASN1;

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
      return ZeEXCEPT(Error, "invalid key data");
    if (hint >= 0 && type != hint)
      return ZeEXCEPT(Error, "inconsistent key data");
    ZmRef<AnyKey> key;
    try {
      switch (type) {
	case Type::PK_X509: {
	  auto hdr = ZtASN1::handler<PK_X509_HDR>(span).ctor();
	  auto id = Load_::matcher.match(hdr.id);
	  if (id < 0) return ZeEXCEPT(Error, ([id = ZtBytes(hdr.id)](auto &s) {
	    ZmHex::enc(id, [&s](ZuCSpan id) {
	      s << "unknown X509 OID " << id;
	    });
	  }));
	  switch (id) {
	    case 0: {
	      auto data = ZtASN1::handler<PK_X509_RSA>(span).ctor();
	      key = new PK_RSA(data.rsa);
	    } break;
	    case 1: {
	      auto data = ZtASN1::handler<PK_X509_EC>(span).ctor();
	      key = new PK_EC(data.id2, data.pubKey);
	    } break;
	    case 2: {
	      auto data = ZtASN1::handler<PK_X509_ED25519>(span).ctor();
	      key = new PK_ED25519(data.pubKey);
	    } break;
	  }
	} break;
	case Type::PK_PKCS1: {
	  auto data = ZtASN1::handler<PK_PKCS1>(span).ctor();
	  key = new PK_RSA(data);
	} break;
      }
    } catch (const ZeException &e) {
      return e;
    }
    if (!key) return ZeEXCEPT(Error, "unknown key type");
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
    using namespace ZtASN1;

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
      return ZeEXCEPT(Error, "invalid key data");
    if (hint >= 0 && type != hint)
      return ZeEXCEPT(Error, "inconsistent key data");
    ZmRef<AnyKey> key;
    try {
      switch (type) {
	case Type::SK_PKCS8: {
	  auto hdr = ZtASN1::handler<SK_PKCS8_HDR>(span).ctor();
	  auto id = Load_::matcher.match(hdr.id);
	  if (id < 0) return ZeEXCEPT(Error, ([id = ZtBytes(hdr.id)](auto &s) {
	    ZmHex::enc(id, [&s](ZuCSpan id) {
	      s << "unknown PKCS#8 OID " << id;
	    });
	  }));
	  switch (id) {
	    case 0: {
	      auto data = ZtASN1::handler<SK_PKCS8_RSA>(span).ctor();
	      key = new SK_RSA(data.rsa);
	    } break;
	    case 1: {
	      auto data = ZtASN1::handler<SK_PKCS8_EC>(span).ctor();
	      key = new SK_EC(m_rng, data.id2, data.ec.key);
	    } break;
	    case 2: {
	      auto data = ZtASN1::handler<SK_PKCS8_ED25519>(span).ctor();
	      key = new SK_ED25519(data.key);
	    } break;
	  }
	} break;
	case Type::SK_SEC1: {
	  auto data = ZtASN1::handler<SK_SEC1>(span).ctor();
	  key = new SK_EC(m_rng, data.id, data.key);
	}
	case Type::SK_PKCS1: {
	  auto data = ZtASN1::handler<SK_PKCS1>(span).ctor();
	  key = new SK_RSA(data);
	}
      }
    } catch (const ZeException &e) {
      return e;
    }
    if (!key) return ZeEXCEPT(Error, "unknown key type");
    return key;
  }

private:
  Random	&m_rng;
};

template <typename S, typename Key>
ZuUnion<void, ZeException> saveB64(S &s, Key *key) {
  using namespace Data;

  auto buf_ = ZmAlloc(char, Data::DERBufSize);
  ZtArray<char> buf(&buf_[0], 0, Data::DERBufSize, false);
  auto r = key->save(buf);
  if (ZuUnlikely(r.template is<ZeException>())) return r;
  ZmBase64::enc(buf.span(), [&s](ZuCSpan b64) { s << b64; });
  return {};
}

template <typename S, typename Key>
ZuUnion<void, ZeException> savePEM(S &s, const Key *key) {
  using namespace Data;

  auto buf_ = ZmAlloc(char, BufSize);
  ZtArray<char> buf(&buf_[0], 0, BufSize, false);
  auto r = key->save(buf);
  if (ZuUnlikely(r.template is<ZeException>())) return r;
  ZmBase64::enc(buf.span(), [&s](ZuCSpan b64) {
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
    return ZeEXCEPT(Error, ([path, e = file.error()](auto &s) {
      s << "\"" << path << "\" - open failed: " << e;
    }));
  auto buf_ = ZmAlloc(char, BufSize);
  ZtArray<char> buf(&buf_[0], 0, BufSize, false);
  auto r = savePEM(buf, key);
  if (ZuUnlikely(r.template is<ZeException>())) return r;
  if (file.write(&buf[0], buf.length()) < buf.length())
    return ZeEXCEPT(Error, ([path, e = file.error()](auto &s) {
      s << "\"" << path << "\" - write failed: " << e;
    }));
  file.close();
  return {};
}

} // Ztls::PK

#endif /* ZtlsPK_HH */
