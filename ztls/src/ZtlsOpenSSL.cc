//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OpenSSL backend implementation

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/bn.h>
#include <openssl/asn1.h>
#include <openssl/core_names.h>
#include <openssl/objects.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <stdio.h>
#include <string.h>

#include <zpicotls/openssl.h>

#include <zlib/ZtlsBackend.hh>
#include <zlib/ZtlsPico.hh>

namespace Ztls::Backend {

struct PKey {
  EVP_PKEY	*pkey = nullptr;
};

struct CertStore {
  X509_STORE	*store = nullptr;
};

struct SignCert {
  ptls_openssl_sign_certificate_t impl{};
};

struct VerifyCert {
  ptls_openssl_verify_certificate_t impl{};
};

struct TicketKey {
  ptls_encrypt_ticket_t super{};
  uint8_t name[16]{};
  uint8_t aes_key[32]{};
  uint8_t hmac_key[32]{};
};

namespace {

struct SHA1Ctx {
  ptls_hash_context_t	super;
  EVP_MD_CTX		*ctx;
};

static void sha1_update(ptls_hash_context_t *ctx_, const void *src, size_t len)
{
  auto ctx = reinterpret_cast<SHA1Ctx *>(ctx_);
  EVP_DigestUpdate(ctx->ctx, src, len);
}

static void sha1_final(ptls_hash_context_t *ctx_, void *md,
    ptls_hash_final_mode_t mode)
{
  auto ctx = reinterpret_cast<SHA1Ctx *>(ctx_);
  uint8_t out[20];
  unsigned outlen = sizeof(out);
  EVP_MD_CTX *target = ctx->ctx;
  if (mode == PTLS_HASH_FINAL_MODE_SNAPSHOT) {
    EVP_MD_CTX *copy = EVP_MD_CTX_new();
    EVP_MD_CTX_copy_ex(copy, ctx->ctx);
    EVP_DigestFinal_ex(copy, out, &outlen);
    EVP_MD_CTX_free(copy);
  } else {
    EVP_DigestFinal_ex(target, out, &outlen);
  }
  if (md) memcpy(md, out, sizeof(out));
  switch (mode) {
    case PTLS_HASH_FINAL_MODE_RESET:
      EVP_DigestInit_ex(ctx->ctx, EVP_sha1(), nullptr);
      break;
    case PTLS_HASH_FINAL_MODE_FREE:
      EVP_MD_CTX_free(ctx->ctx);
      delete ctx;
      break;
    case PTLS_HASH_FINAL_MODE_SNAPSHOT:
      break;
  }
}

static ptls_hash_context_t *sha1_clone(ptls_hash_context_t *src_)
{
  auto src = reinterpret_cast<SHA1Ctx *>(src_);
  auto dst = new SHA1Ctx{};
  dst->super.update = sha1_update;
  dst->super.final = sha1_final;
  dst->super.clone_ = sha1_clone;
  dst->ctx = EVP_MD_CTX_new();
  EVP_MD_CTX_copy_ex(dst->ctx, src->ctx);
  return &dst->super;
}

static ptls_hash_context_t *sha1_create()
{
  auto ctx = new SHA1Ctx{};
  ctx->super.update = sha1_update;
  ctx->super.final = sha1_final;
  ctx->super.clone_ = sha1_clone;
  ctx->ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx->ctx, EVP_sha1(), nullptr);
  return &ctx->super;
}

static ptls_hash_algorithm_t sha1_algo = {
  "sha1",
  64,
  20,
  sha1_create,
  {
    0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d,
    0x32, 0x55, 0xbf, 0xef, 0x95, 0x60, 0x18, 0x90,
    0xaf, 0xd8, 0x07, 0x09
  }
};

static const char *alert_string_(int alert)
{
  switch (alert) {
    case PTLS_ALERT_CLOSE_NOTIFY: return "close_notify";
    case PTLS_ALERT_UNEXPECTED_MESSAGE: return "unexpected_message";
    case PTLS_ALERT_BAD_RECORD_MAC: return "bad_record_mac";
    case PTLS_ALERT_HANDSHAKE_FAILURE: return "handshake_failure";
    case PTLS_ALERT_BAD_CERTIFICATE: return "bad_certificate";
    case PTLS_ALERT_UNSUPPORTED_CERTIFICATE: return "unsupported_certificate";
    case PTLS_ALERT_CERTIFICATE_REVOKED: return "certificate_revoked";
    case PTLS_ALERT_CERTIFICATE_EXPIRED: return "certificate_expired";
    case PTLS_ALERT_CERTIFICATE_UNKNOWN: return "certificate_unknown";
    case PTLS_ALERT_ILLEGAL_PARAMETER: return "illegal_parameter";
    case PTLS_ALERT_UNKNOWN_CA: return "unknown_ca";
    case PTLS_ALERT_ACCESS_DENIED: return "access_denied";
    case PTLS_ALERT_DECODE_ERROR: return "decode_error";
    case PTLS_ALERT_DECRYPT_ERROR: return "decrypt_error";
    case PTLS_ALERT_PROTOCOL_VERSION: return "protocol_version";
    case PTLS_ALERT_INTERNAL_ERROR: return "internal_error";
    case PTLS_ALERT_USER_CANCELED: return "user_canceled";
    case PTLS_ALERT_MISSING_EXTENSION: return "missing_extension";
    case PTLS_ALERT_UNSUPPORTED_EXTENSION: return "unsupported_extension";
    case PTLS_ALERT_UNRECOGNIZED_NAME: return "unrecognized_name";
    case PTLS_ALERT_UNKNOWN_PSK_IDENTITY: return "unknown_psk_identity";
    case PTLS_ALERT_CERTIFICATE_REQUIRED: return "certificate_required";
    case PTLS_ALERT_NO_APPLICATION_PROTOCOL: return "no_application_protocol";
    case PTLS_ALERT_ECH_REQUIRED: return "ech_required";
  }
  return nullptr;
}

static const char *ptls_error_string_(int err)
{
  switch (err) {
    case PTLS_ERROR_NO_MEMORY: return "no_memory";
    case PTLS_ERROR_IN_PROGRESS: return "in_progress";
    case PTLS_ERROR_LIBRARY: return "library_error";
    case PTLS_ERROR_INCOMPATIBLE_KEY: return "incompatible_key";
    case PTLS_ERROR_SESSION_NOT_FOUND: return "session_not_found";
    case PTLS_ERROR_STATELESS_RETRY: return "stateless_retry";
    case PTLS_ERROR_NOT_AVAILABLE: return "not_available";
    case PTLS_ERROR_COMPRESSION_FAILURE: return "compression_failure";
    case PTLS_ERROR_REJECT_EARLY_DATA: return "reject_early_data";
    case PTLS_ERROR_DELEGATE: return "delegate";
    case PTLS_ERROR_ASYNC_OPERATION: return "async_operation";
    case PTLS_ERROR_BLOCK_OVERFLOW: return "block_overflow";
  }
  return nullptr;
}

static const EVP_MD *evp_md_(MDType type)
{
  switch (type) {
    case SHA1: return EVP_sha1();
    case SHA256: return EVP_sha256();
    case SHA384: return EVP_sha384();
    case SHA512: return EVP_sha512();
  }
  return EVP_sha256();
}

static bool bn_write_padded_(const BIGNUM *bn, ZuSpan<uint8_t> out)
{
  if (!bn) return false;
  int n = BN_num_bytes(bn);
  if (n < 0 || static_cast<unsigned>(n) > out.length()) return false;
  memset(out.data(), 0, out.length());
  BN_bn2bin(bn, out.data() + (out.length() - n));
  return true;
}

static BIGNUM *bn_from_span_(ZuBSpan in)
{
  return BN_bin2bn(in.data(), in.length(), nullptr);
}

static bool bn_param_write_padded_(
  const EVP_PKEY *pkey, const char *param, ZuSpan<uint8_t> out)
{
  BIGNUM *bn = nullptr;
  if (EVP_PKEY_get_bn_param(pkey, param, &bn) != 1) return false;
  bool ok = bn_write_padded_(bn, out);
  BN_free(bn);
  return ok;
}

static EVP_PKEY *pkey_fromdata_(const char *type, int selection,
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

static bool pkey_check_(EVP_PKEY *pkey, bool privateKey)
{
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_pkey(nullptr, pkey, nullptr);
  if (!ctx) return false;
  int ok = privateKey ?
    EVP_PKEY_pairwise_check(ctx) : EVP_PKEY_public_check(ctx);
  EVP_PKEY_CTX_free(ctx);
  return ok == 1;
}

static int ec_nid_from_pkey_(const EVP_PKEY *pkey)
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

static bool ec_public_from_private_(int nid, const BIGNUM *priv,
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

static EVP_PKEY *rsa_fromdata_(bool privateKey,
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
    pkey_fromdata_("RSA",
      privateKey ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY, params) : nullptr;
  OSSL_PARAM_free(params);
  OSSL_PARAM_BLD_free(bld);
  if (pkey && !pkey_check_(pkey, privateKey)) {
    EVP_PKEY_free(pkey);
    pkey = nullptr;
  }
  return pkey;
}

static EVP_PKEY *ec_fromdata_(int nid, bool privateKey,
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
    pkey_fromdata_("EC",
      privateKey ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY, params) : nullptr;
  OSSL_PARAM_free(params);
  OSSL_PARAM_BLD_free(bld);
  if (pkey && !pkey_check_(pkey, privateKey)) {
    EVP_PKEY_free(pkey);
    pkey = nullptr;
  }
  return pkey;
}

static int nid_from_oid_(ZuBSpan oid)
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

static bool oid_from_nid_(int nid, ZuSpan<uint8_t> out)
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

static void replace_pkey_(PKey *key, EVP_PKEY *pkey)
{
  if (key->pkey) EVP_PKEY_free(key->pkey);
  key->pkey = pkey;
}

static thread_local TicketKey *ticket_key_tls_ = nullptr;

static int ticket_key_cb_(unsigned char *key_name, unsigned char *iv,
  EVP_CIPHER_CTX *ctx, EVP_MAC_CTX *hctx, int enc)
{
  auto key = ticket_key_tls_;
  if (!key) return -1;
  OSSL_PARAM params[] = {
    OSSL_PARAM_construct_utf8_string(
      OSSL_MAC_PARAM_DIGEST, const_cast<char *>("SHA256"), 0),
    OSSL_PARAM_construct_end()
  };
  if (enc) {
    memcpy(key_name, key->name, sizeof(key->name));
    if (RAND_bytes(
	  iv, EVP_CIPHER_iv_length(EVP_aes_256_cbc())) != 1 ||
	EVP_EncryptInit_ex(
	  ctx, EVP_aes_256_cbc(), nullptr, key->aes_key, iv) != 1)
      return -1;
    if (EVP_MAC_init(hctx, key->hmac_key, sizeof(key->hmac_key), params) != 1)
      return -1;
    return 1;
  }
  if (memcmp(key_name, key->name, sizeof(key->name))) return 0;
  EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key->aes_key, iv);
  if (EVP_MAC_init(hctx, key->hmac_key, sizeof(key->hmac_key), params) != 1)
    return -1;
  return 1;
}

static int ticket_encrypt_cb_(ptls_encrypt_ticket_t *self, ptls_t *,
  int is_encrypt, ptls_buffer_t *dst, ptls_iovec_t src)
{
  ticket_key_tls_ = reinterpret_cast<TicketKey *>(self);
  int ret = is_encrypt ?
    ptls_openssl_encrypt_ticket_evp(dst, src, ticket_key_cb_) :
    ptls_openssl_decrypt_ticket_evp(dst, src, ticket_key_cb_);
  ticket_key_tls_ = nullptr;
  return ret;
}

} // namespace

bool init()
{
  static const bool done = []() {
    Ztls::Pico::install();
    return OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, nullptr) == 1;
  }();
  return done;
}

bool random_bytes(ZuSpan<uint8_t> data)
{
  if (!data.length()) return true;
  return init() &&
    RAND_bytes(data.data(), data.length()) == 1;
}

RandomBytesFn random_bytes_cb()
{
  return ptls_openssl_random_bytes;
}

ptls_key_exchange_algorithm_t **key_exchanges()
{
  return ptls_openssl_key_exchanges_all;
}

ptls_cipher_suite_t **cipher_suites()
{
  return ptls_openssl_cipher_suites;
}

ptls_cipher_suite_t *cipher_suite(uint16_t id)
{
  return ptls_find_cipher_suite(cipher_suites(), id);
}

ptls_cipher_suite_t *tls12_ecdhe_rsa_aes128gcmsha256()
{
  return &ptls_openssl_tls12_ecdhe_rsa_aes128gcmsha256;
}

ptls_cipher_suite_t *tls12_ecdhe_rsa_chacha20poly1305sha256()
{
#if PTLS_OPENSSL_HAVE_CHACHA20_POLY1305
  return &ptls_openssl_tls12_ecdhe_rsa_chacha20poly1305sha256;
#else
  return nullptr;
#endif
}

ptls_hash_algorithm_t *hash_algorithm(MDType type)
{
  switch (type) {
    case SHA1: return &sha1_algo;
    case SHA256: return &ptls_openssl_sha256;
    case SHA384: return &ptls_openssl_sha384;
    case SHA512: return &ptls_openssl_sha512;
  }
  return &ptls_openssl_sha256;
}

size_t hash_size(MDType type)
{
  return hash_algorithm(type)->digest_size;
}

size_t format_error(int err, char *buf, size_t len)
{
  if (!buf || !len) return 0;
  buf[0] = '\0';
  unsigned long oe = ERR_peek_last_error();
  if (oe) {
    ERR_error_string_n(oe, buf, len);
    return strlen(buf);
  }
  if (PTLS_ERROR_GET_CLASS(err) == PTLS_ERROR_CLASS_SELF_ALERT ||
      PTLS_ERROR_GET_CLASS(err) == PTLS_ERROR_CLASS_PEER_ALERT) {
    if (auto alert = alert_string_(PTLS_ERROR_TO_ALERT(err))) {
      return snprintf(buf, len, "alert(%s)", alert);
    }
  }
  if (auto ptls_err = ptls_error_string_(err)) {
    return snprintf(buf, len, "zpicotls(%s)", ptls_err);
  }
  return snprintf(buf, len, "error(%d)", err);
}

PKey *pkey_new() { return new PKey{}; }
void pkey_free(PKey *key)
{
  if (!key) return;
  if (key->pkey) EVP_PKEY_free(key->pkey);
  delete key;
}

size_t pkey_rsa_size(const PKey *key)
{
  if (!key || !key->pkey) return 0;
  BIGNUM *n = nullptr;
  if (EVP_PKEY_get_bn_param(key->pkey, OSSL_PKEY_PARAM_RSA_N, &n) != 1)
    return 0;
  int len = BN_num_bytes(n);
  BN_free(n);
  return len > 0 ? size_t(len) : 0;
}

bool pkey_rsa_generate(PKey *key, unsigned bits)
{
  if (!key) return false;
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
  replace_pkey_(key, pkey);
  return true;
}

bool pkey_rsa_import_public(PKey *key, ZuBSpan modulus, ZuBSpan pubExp)
{
  if (!key) return false;
  BIGNUM *n = bn_from_span_(modulus);
  BIGNUM *e = bn_from_span_(pubExp);
  EVP_PKEY *pkey = (n && e) ? rsa_fromdata_(false, n, e) : nullptr;
  BN_free(n);
  BN_free(e);
  if (!pkey)
    return false;
  replace_pkey_(key, pkey);
  return true;
}

bool pkey_rsa_import_private(
  PKey *key, ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp,
  ZuBSpan prime1, ZuBSpan prime2, ZuBSpan exp1, ZuBSpan exp2, ZuBSpan coeff)
{
  if (!key) return false;
  BIGNUM *n = bn_from_span_(modulus);
  BIGNUM *e = bn_from_span_(pubExp);
  BIGNUM *d = bn_from_span_(prvExp);
  BIGNUM *p = bn_from_span_(prime1);
  BIGNUM *q = bn_from_span_(prime2);
  BIGNUM *dp = bn_from_span_(exp1);
  BIGNUM *dq = bn_from_span_(exp2);
  BIGNUM *qi = bn_from_span_(coeff);
  EVP_PKEY *pkey = (n && e && d && p && q && dp && dq && qi) ?
    rsa_fromdata_(true, n, e, d, p, q, dp, dq, qi) : nullptr;
  BN_free(n);
  BN_free(e);
  BN_free(d);
  BN_free(p);
  BN_free(q);
  BN_free(dp);
  BN_free(dq);
  BN_free(qi);
  if (!pkey) return false;
  replace_pkey_(key, pkey);
  return true;
}

bool pkey_rsa_export_public(
  const PKey *key, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp)
{
  if (!key || !key->pkey) return false;
  return
    bn_param_write_padded_(key->pkey, OSSL_PKEY_PARAM_RSA_N, modulus) &&
    bn_param_write_padded_(key->pkey, OSSL_PKEY_PARAM_RSA_E, pubExp);
}

bool pkey_rsa_export_private(
  const PKey *key, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp,
  ZuSpan<uint8_t> prvExp, ZuSpan<uint8_t> prime1, ZuSpan<uint8_t> prime2,
  ZuSpan<uint8_t> exp1, ZuSpan<uint8_t> exp2, ZuSpan<uint8_t> coeff)
{
  if (!key || !key->pkey) return false;
  return
    bn_param_write_padded_(key->pkey, OSSL_PKEY_PARAM_RSA_N, modulus) &&
    bn_param_write_padded_(key->pkey, OSSL_PKEY_PARAM_RSA_E, pubExp) &&
    bn_param_write_padded_(key->pkey, OSSL_PKEY_PARAM_RSA_D, prvExp) &&
    bn_param_write_padded_(key->pkey, OSSL_PKEY_PARAM_RSA_FACTOR1, prime1) &&
    bn_param_write_padded_(key->pkey, OSSL_PKEY_PARAM_RSA_FACTOR2, prime2) &&
    bn_param_write_padded_(key->pkey, OSSL_PKEY_PARAM_RSA_EXPONENT1, exp1) &&
    bn_param_write_padded_(key->pkey, OSSL_PKEY_PARAM_RSA_EXPONENT2, exp2) &&
    bn_param_write_padded_(key->pkey, OSSL_PKEY_PARAM_RSA_COEFFICIENT1, coeff);
}

size_t pkey_ec_key_size(const PKey *key)
{
  if (!key || !key->pkey) return 0;
  int nid = ec_nid_from_pkey_(key->pkey);
  if (nid == NID_undef) return 0;
  EC_GROUP *group = EC_GROUP_new_by_curve_name(nid);
  if (!group) return 0;
  int bits = EC_GROUP_get_degree(group);
  EC_GROUP_free(group);
  return (bits + 7) >> 3;
}

size_t pkey_ec_public_size(const PKey *key)
{
  if (!key || !key->pkey) return 0;
  size_t len = 0;
  if (EVP_PKEY_get_octet_string_param(
	key->pkey, OSSL_PKEY_PARAM_PUB_KEY, nullptr, 0, &len) != 1)
    return 0;
  return len;
}

size_t pkey_ec_oid_size(const PKey *key)
{
  if (!key || !key->pkey) return 0;
  int nid = ec_nid_from_pkey_(key->pkey);
  ASN1_OBJECT *obj = OBJ_nid2obj(nid);
  if (!obj) return 0;
  int len = OBJ_length(obj);
  return len > 0 ? size_t(len) : 0;
}

bool pkey_ec_generate(PKey *key, ZuBSpan oid)
{
  if (!key) return false;
  int nid = nid_from_oid_(oid);
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
  replace_pkey_(key, pkey);
  return true;
}

bool pkey_ec_import_public(PKey *key, ZuBSpan oid, ZuBSpan pubKey)
{
  if (!key) return false;
  int nid = nid_from_oid_(oid);
  if (nid == NID_undef) return false;
  EVP_PKEY *pkey = ec_fromdata_(nid, false, pubKey);
  if (!pkey) return false;
  replace_pkey_(key, pkey);
  return true;
}

bool pkey_ec_import_private(PKey *key, ZuBSpan oid, ZuBSpan priv)
{
  if (!key) return false;
  int nid = nid_from_oid_(oid);
  if (nid == NID_undef) return false;
  BIGNUM *d = bn_from_span_(priv);
  if (!d) return false;
  uint8_t *pubKey = nullptr;
  size_t pubKeyLen = 0;
  if (!ec_public_from_private_(nid, d, &pubKey, &pubKeyLen)) {
    BN_free(d);
    return false;
  }
  EVP_PKEY *pkey = ec_fromdata_(nid, true, {pubKey, unsigned(pubKeyLen)}, d);
  OPENSSL_free(pubKey);
  BN_free(d);
  if (!pkey) return false;
  replace_pkey_(key, pkey);
  return true;
}

bool pkey_ec_export_private(const PKey *key, ZuSpan<uint8_t> out)
{
  if (!key || !key->pkey) return false;
  return bn_param_write_padded_(key->pkey, OSSL_PKEY_PARAM_PRIV_KEY, out);
}

bool pkey_ec_export_public(const PKey *key, ZuSpan<uint8_t> out)
{
  if (!key || !key->pkey) return false;
  size_t n = 0;
  if (EVP_PKEY_get_octet_string_param(
	key->pkey, OSSL_PKEY_PARAM_PUB_KEY, out.data(), out.length(), &n) != 1)
    return false;
  return n == out.length();
}

bool pkey_ec_export_oid(const PKey *key, ZuSpan<uint8_t> out)
{
  if (!key || !key->pkey) return false;
  int nid = ec_nid_from_pkey_(key->pkey);
  if (nid == NID_undef) return false;
  return oid_from_nid_(nid, out);
}

bool pkey_ed25519_generate(PKey *key)
{
#if defined(EVP_PKEY_ED25519)
  if (!key) return false;
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
  replace_pkey_(key, pkey);
  return true;
#else
  (void)key;
  return false;
#endif
}

bool pkey_ed25519_import_public(PKey *key, ZuBSpan pubKey)
{
#if defined(EVP_PKEY_ED25519)
  if (!key || pubKey.length() != 32) return false;
  EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(
    EVP_PKEY_ED25519, nullptr, pubKey.data(), pubKey.length());
  if (!pkey) return false;
  replace_pkey_(key, pkey);
  return true;
#else
  (void)key;
  (void)pubKey;
  return false;
#endif
}

bool pkey_ed25519_import_private(PKey *key, ZuBSpan privKey)
{
#if defined(EVP_PKEY_ED25519)
  if (!key || privKey.length() != 32) return false;
  EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(
    EVP_PKEY_ED25519, nullptr, privKey.data(), privKey.length());
  if (!pkey) return false;
  replace_pkey_(key, pkey);
  return true;
#else
  (void)key;
  (void)privKey;
  return false;
#endif
}

bool pkey_ed25519_export_public(const PKey *key, ZuSpan<uint8_t> pubKey)
{
#if defined(EVP_PKEY_ED25519)
  if (!key || !key->pkey || pubKey.length() != 32) return false;
  size_t len = pubKey.length();
  return EVP_PKEY_get_raw_public_key(key->pkey, pubKey.data(), &len) == 1 &&
    len == pubKey.length();
#else
  (void)key;
  (void)pubKey;
  return false;
#endif
}

bool pkey_ed25519_export_private(const PKey *key, ZuSpan<uint8_t> privKey)
{
#if defined(EVP_PKEY_ED25519)
  if (!key || !key->pkey || privKey.length() != 32) return false;
  size_t len = privKey.length();
  return EVP_PKEY_get_raw_private_key(key->pkey, privKey.data(), &len) == 1 &&
    len == privKey.length();
#else
  (void)key;
  (void)privKey;
  return false;
#endif
}

bool pkey_sign(
  const PKey *key, MDType md, ZuBSpan data,
  ZuSpan<uint8_t> sig, size_t *siglen)
{
  if (!key || !key->pkey || !siglen) return false;
#if defined(EVP_PKEY_ED25519)
  if (EVP_PKEY_base_id(key->pkey) == EVP_PKEY_ED25519) {
    (void)md;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    size_t len = sig.length();
    int ok = EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key->pkey) == 1 &&
      EVP_DigestSign(ctx, sig.data(), &len, data.data(), data.length()) == 1;
    EVP_MD_CTX_free(ctx);
    if (!ok) return false;
    *siglen = len;
    return true;
  }
#endif
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(key->pkey, nullptr);
  if (!ctx) return false;
  if (EVP_PKEY_sign_init(ctx) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return false;
  }
  if (EVP_PKEY_CTX_set_signature_md(ctx, evp_md_(md)) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return false;
  }
  if (EVP_PKEY_base_id(key->pkey) == EVP_PKEY_RSA) {
    EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING);
  }
  size_t len = sig.length();
  int ok = EVP_PKEY_sign(ctx, sig.data(), &len, data.data(), data.length());
  EVP_PKEY_CTX_free(ctx);
  if (ok <= 0) return false;
  *siglen = len;
  return true;
}

bool pkey_verify(const PKey *key, MDType md, ZuBSpan data, ZuBSpan sig)
{
  if (!key || !key->pkey) return false;
#if defined(EVP_PKEY_ED25519)
  if (EVP_PKEY_base_id(key->pkey) == EVP_PKEY_ED25519) {
    (void)md;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    int ok = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key->pkey) == 1 &&
      EVP_DigestVerify(ctx,
	sig.data(), sig.length(), data.data(), data.length()) == 1;
    EVP_MD_CTX_free(ctx);
    return ok == 1;
  }
#endif
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(key->pkey, nullptr);
  if (!ctx) return false;
  if (EVP_PKEY_verify_init(ctx) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return false;
  }
  if (EVP_PKEY_CTX_set_signature_md(ctx, evp_md_(md)) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return false;
  }
  if (EVP_PKEY_base_id(key->pkey) == EVP_PKEY_RSA) {
    EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING);
  }
  int ok = EVP_PKEY_verify(ctx,
    sig.data(), sig.length(), data.data(), data.length());
  EVP_PKEY_CTX_free(ctx);
  return ok == 1;
}

CertStore *cert_store_new()
{
  auto store = new CertStore{};
  store->store = X509_STORE_new();
  if (!store->store) { delete store; return nullptr; }
  return store;
}

void cert_store_free(CertStore *store)
{
  if (!store) return;
  if (store->store) X509_STORE_free(store->store);
  delete store;
}

bool cert_store_load_path(CertStore *store, const char *path)
{
  if (!store || !store->store || !path) return false;
  return X509_STORE_load_locations(store->store, nullptr, path) == 1;
}

bool cert_store_load_file(CertStore *store, const char *path)
{
  if (!store || !store->store || !path) return false;
  return X509_STORE_load_locations(store->store, path, nullptr) == 1;
}

bool cert_store_add_der(CertStore *store, const uint8_t *data, size_t len)
{
  if (!store || !store->store || !data || !len) return false;
  const unsigned char *p = data;
  X509 *cert = d2i_X509(nullptr, &p, len);
  if (!cert) return false;
  int ok = X509_STORE_add_cert(store->store, cert);
  if (!ok) {
    unsigned long err = ERR_peek_last_error();
    if (ERR_GET_LIB(err) == ERR_LIB_X509 &&
	ERR_GET_REASON(err) == X509_R_CERT_ALREADY_IN_HASH_TABLE)
      ok = 1;
  }
  X509_free(cert);
  return ok == 1;
}

VerifyCert *verify_cert_new(CertStore *store)
{
  if (!store || !store->store) return nullptr;
  auto verify = new VerifyCert{};
  if (ptls_openssl_init_verify_certificate(&verify->impl, store->store) != 0) {
    delete verify;
    return nullptr;
  }
  return verify;
}

void verify_cert_free(VerifyCert *verify)
{
  if (!verify) return;
  ptls_openssl_dispose_verify_certificate(&verify->impl);
  delete verify;
}

ptls_verify_certificate_t *verify_cert_cb(VerifyCert *verify)
{
  return verify ? &verify->impl.super : nullptr;
}

SignCert *sign_cert_new(PKey *key)
{
  if (!key || !key->pkey) return nullptr;
  auto sign = new SignCert{};
  if (ptls_openssl_init_sign_certificate(&sign->impl, key->pkey) != 0) {
    delete sign;
    return nullptr;
  }
  return sign;
}

void sign_cert_free(SignCert *sign)
{
  if (!sign) return;
  ptls_openssl_dispose_sign_certificate(&sign->impl);
  delete sign;
}

ptls_sign_certificate_t *sign_cert_cb(SignCert *sign)
{
  return sign ? &sign->impl.super : nullptr;
}

bool sign_cert_async(SignCert *sign, bool async)
{
  if (!sign) return false;
#ifdef _WIN32
  // zpicotls exposes an int fd for async jobs while Windows HANDLE is pointer-sized.
  if (async) return false;
  sign->impl.async = 0;
  return true;
#else
#if defined(PTLS_OPENSSL_HAVE_ASYNC) && PTLS_OPENSSL_HAVE_ASYNC
  sign->impl.async = async ? 1 : 0;
  return true;
#else
  sign->impl.async = 0;
  return !async;
#endif
#endif
}

PKey *pkey_load_pem(const char *path)
{
  if (!path) return nullptr;
  BIO *bio = BIO_new_file(path, "r");
  if (!bio) return nullptr;
  EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!pkey) return nullptr;
  auto key = new PKey{};
  key->pkey = pkey;
  return key;
}

bool load_certificates(ptls_context_t *ctx, const char *path)
{
  if (!ctx || !path) return false;
  return ptls_load_certificates(ctx, path) == 0;
}

TicketKey *ticket_key_new()
{
  if (!init()) return nullptr;
  auto key = new TicketKey{};
  key->super.cb = ticket_encrypt_cb_;
  if (RAND_bytes(key->name, sizeof(key->name)) != 1 ||
      RAND_bytes(key->aes_key, sizeof(key->aes_key)) != 1 ||
      RAND_bytes(key->hmac_key, sizeof(key->hmac_key)) != 1) {
    delete key;
    return nullptr;
  }
  return key;
}

void ticket_key_free(TicketKey *key)
{
  if (!key) return;
  delete key;
}

ptls_encrypt_ticket_t *ticket_encrypt_cb(TicketKey *key)
{
  return key ? &key->super : nullptr;
}

} // namespace Ztls::Backend
