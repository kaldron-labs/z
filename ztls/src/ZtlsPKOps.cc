//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtlsPKOps.hh>

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/param_build.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <limits.h>
#include <string.h>

namespace Ztls::PK {

Key::~Key() { EVP_PKEY_free(m_key); }

void Key::reset(EVP_PKEY *key)
{
  if (key == m_key) return;
  EVP_PKEY_free(m_key);
  m_key = key;
}

static bool bn_write_padded(const BIGNUM *bn, ZuSpan<uint8_t> out)
{
  if (!bn) return false;
  int n = BN_num_bytes(bn);
  if (n < 0 || static_cast<unsigned>(n) > out.length()) return false;
  memset(out.data(), 0, out.length());
  BN_bn2bin(bn, out.data() + (out.length() - n));
  return true;
}

static BIGNUM *bn_from_span(ZuBSpan in)
{
  return BN_bin2bn(in.data(), in.length(), nullptr);
}

static bool bn_param_write_padded(
  const EVP_PKEY *pkey, const char *param, ZuSpan<uint8_t> out)
{
  BIGNUM *bn = nullptr;
  if (EVP_PKEY_get_bn_param(pkey, param, &bn) != 1) return false;
  bool ok = bn_write_padded(bn, out);
  BN_free(bn);
  return ok;
}

static EVP_PKEY *pkey_fromdata(const char *type, int selection,
    OSSL_PARAM *params)
{
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(nullptr, type, nullptr);
  if (!ctx) return nullptr;
  EVP_PKEY *pkey = nullptr;
  if (EVP_PKEY_fromdata_init(ctx) != 1 ||
      EVP_PKEY_fromdata(ctx, &pkey, selection, params) != 1) {
    if (pkey) EVP_PKEY_free(pkey);
    pkey = nullptr;
  }
  EVP_PKEY_CTX_free(ctx);
  return pkey;
}

static bool checkNative(EVP_PKEY *pkey, bool privateKey)
{
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_pkey(nullptr, pkey, nullptr);
  if (!ctx) return false;
  int ok = privateKey ?
    EVP_PKEY_pairwise_check(ctx) : EVP_PKEY_public_check(ctx);
  EVP_PKEY_CTX_free(ctx);
  return ok == 1;
}

static int ec_nid_from_pkey(const EVP_PKEY *pkey)
{
  char groupName[80];
  size_t len = 0;
  if (EVP_PKEY_get_utf8_string_param(
	pkey, OSSL_PKEY_PARAM_GROUP_NAME, groupName, sizeof(groupName), &len) != 1)
    return NID_undef;
  if (len >= sizeof(groupName)) return NID_undef;
  groupName[len] = '\0';
  return OBJ_sn2nid(groupName);
}

static bool ec_public_from_private(int nid, const BIGNUM *priv,
    uint8_t **pubKey, size_t *pubKeyLen)
{
  *pubKey = nullptr;
  *pubKeyLen = 0;
  EC_GROUP *group = EC_GROUP_new_by_curve_name(nid);
  if (!group) return false;
  EC_POINT *pub = EC_POINT_new(group);
  if (!pub) { EC_GROUP_free(group); return false; }
  bool ok = false;
  if (EC_POINT_mul(group, pub, priv, nullptr, nullptr, nullptr) == 1) {
    size_t n = EC_POINT_point2oct(
      group, pub, POINT_CONVERSION_UNCOMPRESSED, nullptr, 0, nullptr);
    if (n) {
      auto data = static_cast<uint8_t *>(OPENSSL_malloc(n));
      if (data) {
	ok = EC_POINT_point2oct(
	  group, pub, POINT_CONVERSION_UNCOMPRESSED,
	  data, n, nullptr) == n;
	if (ok) {
	  *pubKey = data;
	  *pubKeyLen = n;
	} else {
	  OPENSSL_free(data);
	}
      }
    }
  }
  EC_POINT_free(pub);
  EC_GROUP_free(group);
  return ok;
}

static EVP_PKEY *rsa_fromdata(bool privateKey,
    BIGNUM *n, BIGNUM *e, BIGNUM *d = nullptr,
    BIGNUM *p = nullptr, BIGNUM *q = nullptr,
    BIGNUM *dp = nullptr, BIGNUM *dq = nullptr, BIGNUM *qi = nullptr)
{
  OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
  if (!bld) return nullptr;
  int ok =
    OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, n) == 1 &&
    OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, e) == 1;
  if (ok && privateKey) {
    ok =
      OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_D, d) == 1 &&
      OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_FACTOR1, p) == 1 &&
      OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_FACTOR2, q) == 1 &&
      OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_EXPONENT1, dp) == 1 &&
      OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_EXPONENT2, dq) == 1 &&
      OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_COEFFICIENT1, qi) == 1;
  }
  OSSL_PARAM *params = ok ? OSSL_PARAM_BLD_to_param(bld) : nullptr;
  EVP_PKEY *pkey = params ?
    pkey_fromdata("RSA",
      privateKey ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY, params) : nullptr;
  OSSL_PARAM_free(params);
  OSSL_PARAM_BLD_free(bld);
  if (pkey && !checkNative(pkey, privateKey)) {
    EVP_PKEY_free(pkey);
    pkey = nullptr;
  }
  return pkey;
}

static EVP_PKEY *ec_fromdata(int nid, bool privateKey,
    ZuBSpan pubKey, BIGNUM *priv = nullptr)
{
  const char *groupName = OBJ_nid2sn(nid);
  if (!groupName) return nullptr;
  OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
  if (!bld) return nullptr;
  int ok =
    OSSL_PARAM_BLD_push_utf8_string(
      bld, OSSL_PKEY_PARAM_GROUP_NAME, groupName, 0) == 1 &&
    (!privateKey ||
      OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY, priv) == 1) &&
    OSSL_PARAM_BLD_push_octet_string(
      bld, OSSL_PKEY_PARAM_PUB_KEY, pubKey.data(), pubKey.length()) == 1;
  OSSL_PARAM *params = ok ? OSSL_PARAM_BLD_to_param(bld) : nullptr;
  EVP_PKEY *pkey = params ?
    pkey_fromdata("EC",
      privateKey ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY, params) : nullptr;
  OSSL_PARAM_free(params);
  OSSL_PARAM_BLD_free(bld);
  if (pkey && !checkNative(pkey, privateKey)) {
    EVP_PKEY_free(pkey);
    pkey = nullptr;
  }
  return pkey;
}

static int nid_from_oid(ZuBSpan oid)
{
  if (!oid.length()) return NID_undef;
  unsigned char buf[64];
  size_t len = oid.length();
  size_t off = 0;
  if (len + 2 > sizeof(buf)) return NID_undef;
  buf[off++] = V_ASN1_OBJECT;
  if (len < 128) {
    buf[off++] = static_cast<unsigned char>(len);
  } else {
    unsigned char tmp[8];
    size_t n = 0;
    size_t v = len;
    while (v) {
      tmp[n++] = static_cast<unsigned char>(v & 0xff);
      v >>= 8;
    }
    buf[off++] = static_cast<unsigned char>(0x80 | n);
    while (n--) buf[off++] = tmp[n];
  }
  memcpy(buf + off, oid.data(), len);
  const unsigned char *p = buf;
  ASN1_OBJECT *obj = d2i_ASN1_OBJECT(nullptr, &p, off + len);
  if (!obj) return NID_undef;
  int nid = OBJ_obj2nid(obj);
  ASN1_OBJECT_free(obj);
  return nid;
}

static bool oid_from_nid(int nid, ZuSpan<uint8_t> out)
{
  ASN1_OBJECT *obj = OBJ_nid2obj(nid);
  if (!obj) return false;
  int len = OBJ_length(obj);
  if (len < 0 || static_cast<unsigned>(len) > out.length()) return false;
  const unsigned char *data = OBJ_get0_data(obj);
  if (!data) return false;
  memcpy(out.data(), data, len);
  return true;
}

bool check(const Key &key, bool privateKey)
{
  return key.get() && checkNative(key.get(), privateKey);
}

bool loadPEM(Key &key, const char *path)
{
  if (!path) return false;
  auto bio = BIO_new_file(path, "r");
  if (!bio) return false;
  auto pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!pkey) return false;
  if (!checkNative(pkey, true)) {
    EVP_PKEY_free(pkey);
    return false;
  }
  key.reset(pkey);
  return true;
}

size_t pkey_rsa_size(const Key &key)
{
  if (!key.get()) return 0;
  BIGNUM *n = nullptr;
  if (EVP_PKEY_get_bn_param(key.get(), OSSL_PKEY_PARAM_RSA_N, &n) != 1)
    return 0;
  int len = BN_num_bytes(n);
  BN_free(n);
  return len > 0 ? size_t(len) : 0;
}

bool pkey_rsa_generate(Key &key, unsigned bits)
{
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
  if (!ctx) return false;
  BIGNUM *e = BN_new();
  EVP_PKEY *pkey = nullptr;
  bool ok = e &&
    BN_set_word(e, RSA_F4) == 1 &&
    EVP_PKEY_keygen_init(ctx) == 1 &&
    EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) == 1 &&
    EVP_PKEY_CTX_set1_rsa_keygen_pubexp(ctx, e) == 1 &&
    EVP_PKEY_keygen(ctx, &pkey) == 1 && pkey;
  BN_free(e);
  EVP_PKEY_CTX_free(ctx);
  if (!ok) { if (pkey) EVP_PKEY_free(pkey); return false; }
  key.reset(pkey);
  return true;
}

bool pkey_rsa_import_public(Key &key, ZuBSpan modulus, ZuBSpan pubExp)
{
  BIGNUM *n = bn_from_span(modulus);
  BIGNUM *e = bn_from_span(pubExp);
  EVP_PKEY *pkey = (n && e) ? rsa_fromdata(false, n, e) : nullptr;
  BN_free(n);
  BN_free(e);
  if (!pkey)
    return false;
  key.reset(pkey);
  return true;
}

bool pkey_rsa_import_private(
  Key &key, ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp,
  ZuBSpan prime1, ZuBSpan prime2, ZuBSpan exp1, ZuBSpan exp2, ZuBSpan coeff)
{
  BIGNUM *n = bn_from_span(modulus);
  BIGNUM *e = bn_from_span(pubExp);
  BIGNUM *d = bn_from_span(prvExp);
  BIGNUM *p = bn_from_span(prime1);
  BIGNUM *q = bn_from_span(prime2);
  BIGNUM *dp = bn_from_span(exp1);
  BIGNUM *dq = bn_from_span(exp2);
  BIGNUM *qi = bn_from_span(coeff);
  EVP_PKEY *pkey = (n && e && d && p && q && dp && dq && qi) ?
    rsa_fromdata(true, n, e, d, p, q, dp, dq, qi) : nullptr;
  BN_free(n);
  BN_free(e);
  BN_free(d);
  BN_free(p);
  BN_free(q);
  BN_free(dp);
  BN_free(dq);
  BN_free(qi);
  if (!pkey) return false;
  key.reset(pkey);
  return true;
}

bool pkey_rsa_import_ssh_private(
  Key &key, ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp,
  ZuBSpan coeff, ZuBSpan prime1, ZuBSpan prime2)
{
  BIGNUM *n = bn_from_span(modulus), *e = bn_from_span(pubExp);
  BIGNUM *d = bn_from_span(prvExp), *qi = bn_from_span(coeff);
  BIGNUM *p = bn_from_span(prime1), *q = bn_from_span(prime2);
  BIGNUM *dp = BN_new(), *dq = BN_new(), *minusOne = BN_new();
  BN_CTX *ctx = BN_CTX_new();
  ZuGuard clear{[n, e, d, qi, p, q, dp, dq, minusOne, ctx]() {
    BN_free(n); BN_free(e);
    BN_clear_free(d); BN_clear_free(qi);
    BN_clear_free(p); BN_clear_free(q);
    BN_clear_free(dp); BN_clear_free(dq);
    BN_clear_free(minusOne);
    BN_CTX_free(ctx);
  }};
  if (!n || !e || !d || !qi || !p || !q ||
      !dp || !dq || !minusOne || !ctx ||
      !BN_copy(minusOne, p) || !BN_sub_word(minusOne, 1) ||
      !BN_mod(dp, d, minusOne, ctx) ||
      !BN_copy(minusOne, q) || !BN_sub_word(minusOne, 1) ||
      !BN_mod(dq, d, minusOne, ctx)) return false;
  EVP_PKEY *pkey = rsa_fromdata(true, n, e, d, p, q, dp, dq, qi);
  if (!pkey) return false;
  key.reset(pkey);
  return true;
}

bool pkey_rsa_export_public(
  const Key &key, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp)
{
  if (!key.get()) return false;
  return
    bn_param_write_padded(key.get(), OSSL_PKEY_PARAM_RSA_N, modulus) &&
    bn_param_write_padded(key.get(), OSSL_PKEY_PARAM_RSA_E, pubExp);
}

bool pkey_rsa_export_private(
  const Key &key, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp,
  ZuSpan<uint8_t> prvExp, ZuSpan<uint8_t> prime1, ZuSpan<uint8_t> prime2,
  ZuSpan<uint8_t> exp1, ZuSpan<uint8_t> exp2, ZuSpan<uint8_t> coeff)
{
  if (!key.get()) return false;
  return
    bn_param_write_padded(key.get(), OSSL_PKEY_PARAM_RSA_N, modulus) &&
    bn_param_write_padded(key.get(), OSSL_PKEY_PARAM_RSA_E, pubExp) &&
    bn_param_write_padded(key.get(), OSSL_PKEY_PARAM_RSA_D, prvExp) &&
    bn_param_write_padded(key.get(), OSSL_PKEY_PARAM_RSA_FACTOR1, prime1) &&
    bn_param_write_padded(key.get(), OSSL_PKEY_PARAM_RSA_FACTOR2, prime2) &&
    bn_param_write_padded(key.get(), OSSL_PKEY_PARAM_RSA_EXPONENT1, exp1) &&
    bn_param_write_padded(key.get(), OSSL_PKEY_PARAM_RSA_EXPONENT2, exp2) &&
    bn_param_write_padded(key.get(), OSSL_PKEY_PARAM_RSA_COEFFICIENT1, coeff);
}

static bool rsa_oaep_init(EVP_PKEY_CTX *ctx, ZuBSpan label)
{
  if (label.length() > INT_MAX ||
      EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) != 1 ||
      EVP_PKEY_CTX_set_rsa_oaep_md(ctx, EVP_sha256()) != 1 ||
      EVP_PKEY_CTX_set_rsa_mgf1_md(ctx, EVP_sha256()) != 1)
    return false;
  if (!label.length()) return true;
  void *copy = OPENSSL_memdup(label.data(), label.length());
  if (!copy) return false;
  if (EVP_PKEY_CTX_set0_rsa_oaep_label(
      ctx, copy, int(label.length())) == 1) return true;
  OPENSSL_free(copy);
  return false;
}

bool pkey_rsa_oaep_encrypt(
  const Key &key, ZuBSpan plaintext, ZuBSpan label,
  ZuSpan<uint8_t> ciphertext, size_t *length)
{
  if (!key.get() || !length) return false;
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(key.get(), nullptr);
  if (!ctx) return false;
  size_t n = ciphertext.length();
  bool ok = EVP_PKEY_encrypt_init(ctx) == 1 &&
    rsa_oaep_init(ctx, label) &&
    EVP_PKEY_encrypt(ctx, ciphertext.data(), &n,
      plaintext.data(), plaintext.length()) == 1;
  EVP_PKEY_CTX_free(ctx);
  if (ok) *length = n;
  return ok;
}

bool pkey_rsa_oaep_decrypt(
  const Key &key, ZuBSpan ciphertext, ZuBSpan label,
  ZuSpan<uint8_t> plaintext, size_t *length)
{
  if (!key.get() || !length) return false;
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(key.get(), nullptr);
  if (!ctx) return false;
  size_t n = plaintext.length();
  bool ok = EVP_PKEY_decrypt_init(ctx) == 1 &&
    rsa_oaep_init(ctx, label) &&
    EVP_PKEY_decrypt(ctx, plaintext.data(), &n,
      ciphertext.data(), ciphertext.length()) == 1;
  EVP_PKEY_CTX_free(ctx);
  if (ok) *length = n;
  return ok;
}

size_t pkey_ec_key_size(const Key &key)
{
  if (!key.get()) return 0;
  int nid = ec_nid_from_pkey(key.get());
  if (nid == NID_undef) return 0;
  EC_GROUP *group = EC_GROUP_new_by_curve_name(nid);
  if (!group) return 0;
  int bits = EC_GROUP_get_degree(group);
  EC_GROUP_free(group);
  return (bits + 7) >> 3;
}

size_t pkey_ec_public_size(const Key &key)
{
  if (!key.get()) return 0;
  size_t len = 0;
  if (EVP_PKEY_get_octet_string_param(
	key.get(), OSSL_PKEY_PARAM_PUB_KEY, nullptr, 0, &len) != 1)
    return 0;
  return len;
}

size_t pkey_ec_oid_size(const Key &key)
{
  if (!key.get()) return 0;
  int nid = ec_nid_from_pkey(key.get());
  ASN1_OBJECT *obj = OBJ_nid2obj(nid);
  if (!obj) return 0;
  int len = OBJ_length(obj);
  return len > 0 ? size_t(len) : 0;
}

bool pkey_ec_generate(Key &key, ZuBSpan oid)
{
  int nid = nid_from_oid(oid);
  if (nid == NID_undef) return false;
  const char *groupName = OBJ_nid2sn(nid);
  if (!groupName) return false;
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
  if (!ctx) return false;
  EVP_PKEY *pkey = nullptr;
  bool ok = EVP_PKEY_keygen_init(ctx) == 1 &&
    EVP_PKEY_CTX_set_group_name(ctx, groupName) == 1 &&
    EVP_PKEY_keygen(ctx, &pkey) == 1 && pkey;
  EVP_PKEY_CTX_free(ctx);
  if (!ok) { if (pkey) EVP_PKEY_free(pkey); return false; }
  key.reset(pkey);
  return true;
}

bool pkey_ec_import_public(Key &key, ZuBSpan oid, ZuBSpan pubKey)
{
  int nid = nid_from_oid(oid);
  if (nid == NID_undef) return false;
  EVP_PKEY *pkey = ec_fromdata(nid, false, pubKey);
  if (!pkey) return false;
  key.reset(pkey);
  return true;
}

bool pkey_ec_import_private(Key &key, ZuBSpan oid, ZuBSpan priv)
{
  int nid = nid_from_oid(oid);
  if (nid == NID_undef) return false;
  BIGNUM *d = bn_from_span(priv);
  if (!d) return false;
  uint8_t *pubKey = nullptr;
  size_t pubKeyLen = 0;
  if (!ec_public_from_private(nid, d, &pubKey, &pubKeyLen)) {
    BN_free(d);
    return false;
  }
  EVP_PKEY *pkey = ec_fromdata(nid, true, {pubKey, unsigned(pubKeyLen)}, d);
  OPENSSL_free(pubKey);
  BN_free(d);
  if (!pkey) return false;
  key.reset(pkey);
  return true;
}

bool pkey_ec_export_private(const Key &key, ZuSpan<uint8_t> out)
{
  if (!key.get()) return false;
  return bn_param_write_padded(key.get(), OSSL_PKEY_PARAM_PRIV_KEY, out);
}

bool pkey_ec_export_public(const Key &key, ZuSpan<uint8_t> out)
{
  if (!key.get()) return false;
  size_t n = 0;
  if (EVP_PKEY_get_octet_string_param(
	key.get(), OSSL_PKEY_PARAM_PUB_KEY, out.data(), out.length(), &n) != 1)
    return false;
  return n == out.length();
}

bool pkey_ec_export_oid(const Key &key, ZuSpan<uint8_t> out)
{
  if (!key.get()) return false;
  int nid = ec_nid_from_pkey(key.get());
  if (nid == NID_undef) return false;
  return oid_from_nid(nid, out);
}

bool pkey_ed25519_generate(Key &key)
{
#if defined(EVP_PKEY_ED25519)
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  if (!ctx) return false;
  EVP_PKEY *pkey = nullptr;
  bool ok = EVP_PKEY_keygen_init(ctx) == 1 &&
    EVP_PKEY_keygen(ctx, &pkey) == 1 && pkey;
  EVP_PKEY_CTX_free(ctx);
  if (!ok) {
    if (pkey) EVP_PKEY_free(pkey);
    return false;
  }
  key.reset(pkey);
  return true;
#else
  (void)key;
  return false;
#endif
}

bool pkey_ed25519_import_public(Key &key, ZuBSpan pubKey)
{
#if defined(EVP_PKEY_ED25519)
  if (pubKey.length() != X25519KeySize) return false;
  EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(
    EVP_PKEY_ED25519, nullptr, pubKey.data(), pubKey.length());
  if (!pkey) return false;
  key.reset(pkey);
  return true;
#else
  (void)key;
  (void)pubKey;
  return false;
#endif
}

bool pkey_ed25519_import_private(Key &key, ZuBSpan privKey)
{
#if defined(EVP_PKEY_ED25519)
  if (privKey.length() != 32) return false;
  EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(
    EVP_PKEY_ED25519, nullptr, privKey.data(), privKey.length());
  if (!pkey) return false;
  key.reset(pkey);
  return true;
#else
  (void)key;
  (void)privKey;
  return false;
#endif
}

bool pkey_ed25519_export_public(const Key &key, ZuSpan<uint8_t> pubKey)
{
#if defined(EVP_PKEY_ED25519)
  if (!key.get() || pubKey.length() != 32) return false;
  size_t len = pubKey.length();
  return EVP_PKEY_get_raw_public_key(key.get(), pubKey.data(), &len) == 1 &&
    len == pubKey.length();
#else
  (void)key;
  (void)pubKey;
  return false;
#endif
}

bool pkey_ed25519_export_private(const Key &key, ZuSpan<uint8_t> privKey)
{
#if defined(EVP_PKEY_ED25519)
  if (!key.get() || privKey.length() != 32) return false;
  size_t len = privKey.length();
  return EVP_PKEY_get_raw_private_key(key.get(), privKey.data(), &len) == 1 &&
    len == privKey.length();
#else
  (void)key;
  (void)privKey;
  return false;
#endif
}

bool ed25519_to_x25519(
  ZuBSpan edPublic, ZuSpan<uint8_t> xPublic)
{
  if (edPublic.length() != 32 || xPublic.length() != 32) return false;
  uint8_t encoded[32];
  memcpy(encoded, edPublic.data(), sizeof(encoded));
  bool sign = encoded[31] & 0x80;
  encoded[31] &= 0x7f;
  BN_CTX *ctx = BN_CTX_new();
  if (!ctx) return false;
  ZuGuard release{[ctx]() { BN_CTX_free(ctx); }};
  BN_CTX_start(ctx);
  ZuGuard finish{[ctx]() { BN_CTX_end(ctx); }};
  BIGNUM *p = BN_CTX_get(ctx), *y = BN_CTX_get(ctx);
  BIGNUM *y2 = BN_CTX_get(ctx), *d = BN_CTX_get(ctx);
  BIGNUM *num = BN_CTX_get(ctx), *den = BN_CTX_get(ctx);
  BIGNUM *inv = BN_CTX_get(ctx), *x2 = BN_CTX_get(ctx);
  BIGNUM *x = BN_CTX_get(ctx), *one = BN_CTX_get(ctx);
  BIGNUM *u = BN_CTX_get(ctx), *tmp = BN_CTX_get(ctx);
  if (!tmp || !BN_one(p) || !BN_lshift(p, p, 255) ||
      !BN_sub_word(p, 19) ||
      !BN_lebin2bn(encoded, sizeof(encoded), y) ||
      BN_cmp(y, p) >= 0 || !BN_one(one) ||
      !BN_set_word(d, 121665) || !BN_set_word(tmp, 121666) ||
      !BN_mod_inverse(inv, tmp, p, ctx) ||
      !BN_mod_mul(d, d, inv, p, ctx) ||
      !BN_sub(d, p, d) ||
      !BN_mod_sqr(y2, y, p, ctx) ||
      !BN_mod_sub(num, y2, one, p, ctx) ||
      !BN_mod_mul(den, d, y2, p, ctx) ||
      !BN_mod_add(den, den, one, p, ctx) ||
      !BN_mod_inverse(inv, den, p, ctx) ||
      !BN_mod_mul(x2, num, inv, p, ctx) ||
      !BN_mod_sqrt(x, x2, p, ctx) ||
      (BN_is_zero(x) && sign) ||
      !BN_mod_sub(den, one, y, p, ctx) ||
      !BN_mod_inverse(inv, den, p, ctx) ||
      !BN_mod_add(num, one, y, p, ctx) ||
      !BN_mod_mul(u, num, inv, p, ctx)) return false;
  return BN_bn2lebinpad(u, xPublic.data(), xPublic.length()) == 32;
}

bool pkey_x25519_generate(Key &key)
{
  auto ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
  if (!ctx) return false;
  EVP_PKEY *pkey = nullptr;
  bool ok = EVP_PKEY_keygen_init(ctx) == 1 &&
    EVP_PKEY_keygen(ctx, &pkey) == 1 && pkey;
  EVP_PKEY_CTX_free(ctx);
  if (!ok) {
    EVP_PKEY_free(pkey);
    return false;
  }
  key.reset(pkey);
  return true;
}

bool pkey_x25519_import_public(Key &key, ZuBSpan pubKey)
{
  if (pubKey.length() != 32) return false;
  auto pkey = EVP_PKEY_new_raw_public_key(
    EVP_PKEY_X25519, nullptr, pubKey.data(), pubKey.length());
  if (!pkey) return false;
  key.reset(pkey);
  return true;
}

bool pkey_x25519_import_private(Key &key, ZuBSpan prvKey)
{
  if (prvKey.length() != X25519KeySize) return false;
  auto pkey = EVP_PKEY_new_raw_private_key(
    EVP_PKEY_X25519, nullptr, prvKey.data(), prvKey.length());
  if (!pkey) return false;
  key.reset(pkey);
  return true;
}

bool pkey_x25519_export_public(const Key &key, ZuSpan<uint8_t> pubKey)
{
  if (!key.get() || pubKey.length() != X25519KeySize) return false;
  size_t n = pubKey.length();
  return EVP_PKEY_get_raw_public_key(key.get(), pubKey.data(), &n) == 1 &&
    n == pubKey.length();
}

bool pkey_x25519_export_private(const Key &key, ZuSpan<uint8_t> prvKey)
{
  if (!key.get() || prvKey.length() != X25519KeySize) return false;
  size_t n = prvKey.length();
  bool ok = EVP_PKEY_get_raw_private_key(key.get(), prvKey.data(), &n) == 1 &&
    n == prvKey.length();
  if (!ok) ZuClear(prvKey);
  return ok;
}

bool pkey_x25519_agree(
  const Key &key, ZuBSpan peerPublic, ZuSpan<uint8_t> secret)
{
  if (!key.get() || peerPublic.length() != X25519KeySize ||
      secret.length() != X25519KeySize) return false;
  auto peer = EVP_PKEY_new_raw_public_key(
    EVP_PKEY_X25519, nullptr, peerPublic.data(), peerPublic.length());
  if (!peer) return false;
  auto ctx = EVP_PKEY_CTX_new(key.get(), nullptr);
  size_t n = secret.length();
  bool ok = ctx && EVP_PKEY_derive_init(ctx) == 1 &&
    EVP_PKEY_derive_set_peer(ctx, peer) == 1 &&
    EVP_PKEY_derive(ctx, secret.data(), &n) == 1 && n == secret.length();
  EVP_PKEY_CTX_free(ctx);
  EVP_PKEY_free(peer);
  if (ok) {
    uint8_t nonzero = 0;
    for (unsigned i = 0; i < secret.length(); i++) nonzero |= secret[i];
    ok = nonzero != 0;
  }
  if (!ok) ZuClear(secret);
  return ok;
}

bool pkey_mlkem768_generate(Key &key)
{
  auto ctx = EVP_PKEY_CTX_new_from_name(nullptr, "ML-KEM-768", nullptr);
  if (!ctx) return false;
  EVP_PKEY *pkey = nullptr;
  bool ok = EVP_PKEY_keygen_init(ctx) == 1 &&
    EVP_PKEY_keygen(ctx, &pkey) == 1 && pkey;
  EVP_PKEY_CTX_free(ctx);
  if (!ok) {
    EVP_PKEY_free(pkey);
    return false;
  }
  key.reset(pkey);
  return true;
}

bool pkey_mlkem768_import_seed(Key &key, ZuBSpan seed)
{
  if (seed.length() != 64) return false;
  auto ctx = EVP_PKEY_CTX_new_from_name(nullptr, "ML-KEM-768", nullptr);
  if (!ctx) return false;
  auto param = OSSL_PARAM_construct_octet_string(
    OSSL_PKEY_PARAM_ML_KEM_SEED, const_cast<uint8_t *>(seed.data()),
    seed.length());
  OSSL_PARAM params[] = {param, OSSL_PARAM_construct_end()};
  EVP_PKEY *pkey = nullptr;
  bool ok = EVP_PKEY_keygen_init(ctx) == 1 &&
    EVP_PKEY_CTX_set_params(ctx, params) == 1 &&
    EVP_PKEY_keygen(ctx, &pkey) == 1 && pkey;
  EVP_PKEY_CTX_free(ctx);
  if (!ok) {
    EVP_PKEY_free(pkey);
    return false;
  }
  key.reset(pkey);
  return true;
}

static bool pkey_mlkem768_import(Key &key, ZuBSpan bytes,
  const char *name, int selection)
{
  auto ctx = EVP_PKEY_CTX_new_from_name(nullptr, "ML-KEM-768", nullptr);
  if (!ctx) return false;
  auto param = OSSL_PARAM_construct_octet_string(
    name, const_cast<uint8_t *>(bytes.data()), bytes.length());
  OSSL_PARAM params[] = {param, OSSL_PARAM_construct_end()};
  EVP_PKEY *pkey = nullptr;
  bool ok = EVP_PKEY_fromdata_init(ctx) == 1 &&
    EVP_PKEY_fromdata(ctx, &pkey, selection, params) == 1 && pkey;
  EVP_PKEY_CTX_free(ctx);
  if (!ok) {
    EVP_PKEY_free(pkey);
    return false;
  }
  key.reset(pkey);
  return true;
}

bool pkey_mlkem768_import_private(Key &key, ZuBSpan prvKey)
{
  return prvKey.length() == 2400 && pkey_mlkem768_import(
    key, prvKey, OSSL_PKEY_PARAM_PRIV_KEY, EVP_PKEY_KEYPAIR);
}

bool pkey_mlkem768_import_public(Key &key, ZuBSpan pubKey)
{
  return pubKey.length() == 1184 && pkey_mlkem768_import(
    key, pubKey, OSSL_PKEY_PARAM_PUB_KEY, EVP_PKEY_PUBLIC_KEY);
}

bool pkey_mlkem768_export_private(
  const Key &key, ZuSpan<uint8_t> prvKey, size_t *length)
{
  if (!key.get() || !length || prvKey.length() < 2400)
    return false;
  size_t n = 0;
  if (EVP_PKEY_get_octet_string_param(key.get(),
      OSSL_PKEY_PARAM_ML_KEM_SEED, prvKey.data(), 64, &n) == 1 && n == 64) {
    *length = n;
    return true;
  }
  n = prvKey.length();
  if (EVP_PKEY_get_octet_string_param(key.get(),
      OSSL_PKEY_PARAM_PRIV_KEY, prvKey.data(), n, &n) != 1 || n != 2400)
    return false;
  *length = n;
  return true;
}

bool pkey_mlkem768_export_public(const Key &key, ZuSpan<uint8_t> pubKey)
{
  if (!key.get() || pubKey.length() != 1184) return false;
  size_t n = pubKey.length();
  return EVP_PKEY_get_octet_string_param(key.get(),
    OSSL_PKEY_PARAM_PUB_KEY, pubKey.data(), n, &n) == 1 && n == 1184;
}

bool pkey_mlkem768_encapsulate(const Key &key,
  ZuSpan<uint8_t> ciphertext, ZuSpan<uint8_t> secret)
{
  if (!key.get() || ciphertext.length() != 1088 ||
      secret.length() != 32) return false;
  auto ctx = EVP_PKEY_CTX_new_from_pkey(nullptr, key.get(), nullptr);
  if (!ctx) return false;
  size_t ctLen = ciphertext.length(), secretLen = secret.length();
  bool ok = EVP_PKEY_encapsulate_init(ctx, nullptr) == 1 &&
    EVP_PKEY_encapsulate(ctx, ciphertext.data(), &ctLen,
      secret.data(), &secretLen) == 1 &&
    ctLen == ciphertext.length() && secretLen == secret.length();
  EVP_PKEY_CTX_free(ctx);
  return ok;
}

bool pkey_mlkem768_decapsulate(const Key &key,
  ZuBSpan ciphertext, ZuSpan<uint8_t> secret)
{
  if (!key.get() || ciphertext.length() != 1088 ||
      secret.length() != 32) return false;
  auto ctx = EVP_PKEY_CTX_new_from_pkey(nullptr, key.get(), nullptr);
  if (!ctx) return false;
  size_t n = secret.length();
  bool ok = EVP_PKEY_decapsulate_init(ctx, nullptr) == 1 &&
    EVP_PKEY_decapsulate(ctx, secret.data(), &n,
      ciphertext.data(), ciphertext.length()) == 1 && n == secret.length();
  EVP_PKEY_CTX_free(ctx);
  return ok;
}

bool pkey_sign(
  const Key &key, MDType md, ZuBSpan data,
  ZuSpan<uint8_t> sig, size_t *siglen)
{
  if (!key.get() || !siglen) return false;
#if defined(EVP_PKEY_ED25519)
  if (EVP_PKEY_base_id(key.get()) == EVP_PKEY_ED25519) {
    (void)md;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    size_t len = sig.length();
    int ok = EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key.get()) == 1 &&
      EVP_DigestSign(ctx, sig.data(), &len, data.data(), data.length()) == 1;
    EVP_MD_CTX_free(ctx);
    if (!ok) return false;
    *siglen = len;
    return true;
  }
#endif
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(key.get(), nullptr);
  if (!ctx) return false;
  if (EVP_PKEY_sign_init(ctx) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return false;
  }
  if (EVP_PKEY_CTX_set_signature_md(ctx, evpMD(md)) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return false;
  }
  if (EVP_PKEY_base_id(key.get()) == EVP_PKEY_RSA) {
    EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING);
  }
  size_t len = sig.length();
  int ok = EVP_PKEY_sign(ctx, sig.data(), &len, data.data(), data.length());
  EVP_PKEY_CTX_free(ctx);
  if (ok <= 0) return false;
  *siglen = len;
  return true;
}

bool pkey_verify(const Key &key, MDType md, ZuBSpan data, ZuBSpan sig)
{
  if (!key.get()) return false;
#if defined(EVP_PKEY_ED25519)
  if (EVP_PKEY_base_id(key.get()) == EVP_PKEY_ED25519) {
    (void)md;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    int ok = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key.get()) == 1 &&
      EVP_DigestVerify(ctx,
	sig.data(), sig.length(), data.data(), data.length()) == 1;
    EVP_MD_CTX_free(ctx);
    return ok == 1;
  }
#endif
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(key.get(), nullptr);
  if (!ctx) return false;
  if (EVP_PKEY_verify_init(ctx) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return false;
  }
  if (EVP_PKEY_CTX_set_signature_md(ctx, evpMD(md)) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return false;
  }
  if (EVP_PKEY_base_id(key.get()) == EVP_PKEY_RSA) {
    EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING);
  }
  int ok = EVP_PKEY_verify(ctx,
    sig.data(), sig.length(), data.data(), data.length());
  EVP_PKEY_CTX_free(ctx);
  return ok == 1;
}

bool pkey_csr(const Key &key, void *ctx, CSRFn fn)
{
  if (!key.get() || !fn || !checkNative(key.get(), true))
    return false;

  const EVP_MD *md = nullptr;
  switch (EVP_PKEY_base_id(key.get())) {
    case EVP_PKEY_RSA:
    case EVP_PKEY_EC:
      md = EVP_sha256();
      break;
#if defined(EVP_PKEY_ED25519)
    case EVP_PKEY_ED25519:
      break;
#endif
    default:
      return false;
  }

  X509_REQ *req = X509_REQ_new();
  X509_NAME *subject = X509_NAME_new();
  unsigned char *der = nullptr;
  int len = 0;
  bool ok = req && subject &&
    X509_REQ_set_version(req, 0) == 1 &&
    X509_REQ_set_subject_name(req, subject) == 1 &&
    X509_REQ_set_pubkey(req, key.get()) == 1 &&
    X509_REQ_sign(req, key.get(), md) > 0 &&
    (len = i2d_X509_REQ(req, &der)) > 0 &&
    fn(ctx, ZuBSpan{der, static_cast<unsigned>(len)});
  OPENSSL_free(der);
  X509_NAME_free(subject);
  X509_REQ_free(req);
  return ok;
}

} // Ztls::PK
