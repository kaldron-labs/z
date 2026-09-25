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
#include <openssl/x509v3.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <zpicotls/openssl.h>

#include <zlib/ZmSpecific.hh>

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

static int ticket_key_cb_(unsigned char *key_name, unsigned char *iv,
  EVP_CIPHER_CTX *ctx, EVP_MAC_CTX *hctx, int enc)
{
  auto key = ZmTLS<TicketKey *, (int TicketKey::*){}>();
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
  auto &key = ZmTLS<TicketKey *, (int TicketKey::*){}>();
  key = reinterpret_cast<TicketKey *>(self);
  int ret = is_encrypt ?
    ptls_openssl_encrypt_ticket_evp(dst, src, ticket_key_cb_) :
    ptls_openssl_decrypt_ticket_evp(dst, src, ticket_key_cb_);
  key = nullptr;
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

ptls_aead_algorithm_t *chacha20poly1305()
{
#if PTLS_OPENSSL_HAVE_CHACHA20_POLY1305
  return &ptls_openssl_chacha20poly1305;
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

bool scrypt(
  ZuBSpan password, ZuBSpan salt, uint64_t n,
  unsigned r, unsigned p, uint64_t maxMem, ZuSpan<uint8_t> output)
{
  bool ok = EVP_PBE_scrypt(
    reinterpret_cast<const char *>(password.data()), password.length(),
    salt.data(), salt.length(), n, r, p, maxMem,
    output.data(), output.length()) == 1;
  if (!ok) ZuClear(output.data(), output.length());
  return ok;
}

bool shake256(ZuBSpan input, ZuSpan<uint8_t> output)
{
  auto ctx = EVP_MD_CTX_new();
  if (!ctx) return false;
  bool ok = EVP_DigestInit_ex(ctx, EVP_shake256(), nullptr) == 1 &&
    EVP_DigestUpdate(ctx, input.data(), input.length()) == 1 &&
    EVP_DigestFinalXOF(ctx, output.data(), output.length()) == 1;
  EVP_MD_CTX_free(ctx);
  if (!ok) ZuClear(output.data(), output.length());
  return ok;
}

bool sha3_256(ZuSpan<const ZuBSpan> input, ZuSpan<uint8_t> output)
{
  if (output.length() != 32) return false;
  auto ctx = EVP_MD_CTX_new();
  if (!ctx) return false;
  bool ok = EVP_DigestInit_ex(ctx, EVP_sha3_256(), nullptr) == 1;
  for (unsigned i = 0, n = input.length(); ok && i < n; i++)
    ok = EVP_DigestUpdate(ctx, input[i].data(), input[i].length()) == 1;
  unsigned n = 0;
  ok = ok && EVP_DigestFinal_ex(ctx, output.data(), &n) == 1 &&
    n == output.length();
  EVP_MD_CTX_free(ctx);
  if (!ok) ZuClear(output.data(), output.length());
  return ok;
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

bool pkey_rsa_import_ssh_private(
  PKey *key, ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp,
  ZuBSpan coeff, ZuBSpan prime1, ZuBSpan prime2)
{
  if (!key) return false;
  BIGNUM *n = bn_from_span_(modulus), *e = bn_from_span_(pubExp);
  BIGNUM *d = bn_from_span_(prvExp), *qi = bn_from_span_(coeff);
  BIGNUM *p = bn_from_span_(prime1), *q = bn_from_span_(prime2);
  BIGNUM *dp = BN_new(), *dq = BN_new(), *minusOne = BN_new();
  BN_CTX *ctx = BN_CTX_new();
  ZuGuard clear{[&]() {
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
  EVP_PKEY *pkey = rsa_fromdata_(true, n, e, d, p, q, dp, dq, qi);
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

static bool rsa_oaep_init_(EVP_PKEY_CTX *ctx, ZuBSpan label)
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
  const PKey *key, ZuBSpan plaintext, ZuBSpan label,
  ZuSpan<uint8_t> ciphertext, size_t *length)
{
  if (!key || !key->pkey || !length) return false;
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(key->pkey, nullptr);
  if (!ctx) return false;
  size_t n = ciphertext.length();
  bool ok = EVP_PKEY_encrypt_init(ctx) == 1 &&
    rsa_oaep_init_(ctx, label) &&
    EVP_PKEY_encrypt(ctx, ciphertext.data(), &n,
      plaintext.data(), plaintext.length()) == 1;
  EVP_PKEY_CTX_free(ctx);
  if (ok) *length = n;
  return ok;
}

bool pkey_rsa_oaep_decrypt(
  const PKey *key, ZuBSpan ciphertext, ZuBSpan label,
  ZuSpan<uint8_t> plaintext, size_t *length)
{
  if (!key || !key->pkey || !length) return false;
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(key->pkey, nullptr);
  if (!ctx) return false;
  size_t n = plaintext.length();
  bool ok = EVP_PKEY_decrypt_init(ctx) == 1 &&
    rsa_oaep_init_(ctx, label) &&
    EVP_PKEY_decrypt(ctx, plaintext.data(), &n,
      ciphertext.data(), ciphertext.length()) == 1;
  EVP_PKEY_CTX_free(ctx);
  if (ok) *length = n;
  return ok;
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
  if (!key || pubKey.length() != X25519KeySize) return false;
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

bool pkey_x25519_generate(PKey *key)
{
  if (!key) return false;
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
  replace_pkey_(key, pkey);
  return true;
}

bool pkey_x25519_import_public(PKey *key, ZuBSpan pubKey)
{
  if (!key || pubKey.length() != 32) return false;
  auto pkey = EVP_PKEY_new_raw_public_key(
    EVP_PKEY_X25519, nullptr, pubKey.data(), pubKey.length());
  if (!pkey) return false;
  replace_pkey_(key, pkey);
  return true;
}

bool pkey_x25519_import_private(PKey *key, ZuBSpan prvKey)
{
  if (!key || prvKey.length() != X25519KeySize) return false;
  auto pkey = EVP_PKEY_new_raw_private_key(
    EVP_PKEY_X25519, nullptr, prvKey.data(), prvKey.length());
  if (!pkey) return false;
  replace_pkey_(key, pkey);
  return true;
}

bool pkey_x25519_export_public(const PKey *key, ZuSpan<uint8_t> pubKey)
{
  if (!key || !key->pkey || pubKey.length() != X25519KeySize) return false;
  size_t n = pubKey.length();
  return EVP_PKEY_get_raw_public_key(key->pkey, pubKey.data(), &n) == 1 &&
    n == pubKey.length();
}

bool pkey_x25519_export_private(const PKey *key, ZuSpan<uint8_t> prvKey)
{
  if (!key || !key->pkey || prvKey.length() != X25519KeySize) return false;
  size_t n = prvKey.length();
  bool ok = EVP_PKEY_get_raw_private_key(key->pkey, prvKey.data(), &n) == 1 &&
    n == prvKey.length();
  if (!ok) ZuClear(prvKey.data(), prvKey.length());
  return ok;
}

bool pkey_x25519_agree(
  const PKey *key, ZuBSpan peerPublic, ZuSpan<uint8_t> secret)
{
  if (!key || !key->pkey || peerPublic.length() != X25519KeySize ||
      secret.length() != X25519KeySize) return false;
  auto peer = EVP_PKEY_new_raw_public_key(
    EVP_PKEY_X25519, nullptr, peerPublic.data(), peerPublic.length());
  if (!peer) return false;
  auto ctx = EVP_PKEY_CTX_new(key->pkey, nullptr);
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
  if (!ok) ZuClear(secret.data(), secret.length());
  return ok;
}

bool pkey_mlkem768_generate(PKey *key)
{
  if (!key) return false;
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
  replace_pkey_(key, pkey);
  return true;
}

bool pkey_mlkem768_import_seed(PKey *key, ZuBSpan seed)
{
  if (!key || seed.length() != 64) return false;
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
  replace_pkey_(key, pkey);
  return true;
}

static bool pkey_mlkem768_import_(PKey *key, ZuBSpan bytes,
  const char *name, int selection)
{
  if (!key) return false;
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
  replace_pkey_(key, pkey);
  return true;
}

bool pkey_mlkem768_import_private(PKey *key, ZuBSpan prvKey)
{
  return prvKey.length() == 2400 && pkey_mlkem768_import_(
    key, prvKey, OSSL_PKEY_PARAM_PRIV_KEY, EVP_PKEY_KEYPAIR);
}

bool pkey_mlkem768_import_public(PKey *key, ZuBSpan pubKey)
{
  return pubKey.length() == 1184 && pkey_mlkem768_import_(
    key, pubKey, OSSL_PKEY_PARAM_PUB_KEY, EVP_PKEY_PUBLIC_KEY);
}

bool pkey_mlkem768_export_private(
  const PKey *key, ZuSpan<uint8_t> prvKey, size_t *length)
{
  if (!key || !key->pkey || !length || prvKey.length() < 2400)
    return false;
  size_t n = 0;
  if (EVP_PKEY_get_octet_string_param(key->pkey,
      OSSL_PKEY_PARAM_ML_KEM_SEED, prvKey.data(), 64, &n) == 1 && n == 64) {
    *length = n;
    return true;
  }
  n = prvKey.length();
  if (EVP_PKEY_get_octet_string_param(key->pkey,
      OSSL_PKEY_PARAM_PRIV_KEY, prvKey.data(), n, &n) != 1 || n != 2400)
    return false;
  *length = n;
  return true;
}

bool pkey_mlkem768_export_public(const PKey *key, ZuSpan<uint8_t> pubKey)
{
  if (!key || !key->pkey || pubKey.length() != 1184) return false;
  size_t n = pubKey.length();
  return EVP_PKEY_get_octet_string_param(key->pkey,
    OSSL_PKEY_PARAM_PUB_KEY, pubKey.data(), n, &n) == 1 && n == 1184;
}

bool pkey_mlkem768_encapsulate(const PKey *key,
  ZuSpan<uint8_t> ciphertext, ZuSpan<uint8_t> secret)
{
  if (!key || !key->pkey || ciphertext.length() != 1088 ||
      secret.length() != 32) return false;
  auto ctx = EVP_PKEY_CTX_new_from_pkey(nullptr, key->pkey, nullptr);
  if (!ctx) return false;
  size_t ctLen = ciphertext.length(), secretLen = secret.length();
  bool ok = EVP_PKEY_encapsulate_init(ctx, nullptr) == 1 &&
    EVP_PKEY_encapsulate(ctx, ciphertext.data(), &ctLen,
      secret.data(), &secretLen) == 1 &&
    ctLen == ciphertext.length() && secretLen == secret.length();
  EVP_PKEY_CTX_free(ctx);
  return ok;
}

bool pkey_mlkem768_decapsulate(const PKey *key,
  ZuBSpan ciphertext, ZuSpan<uint8_t> secret)
{
  if (!key || !key->pkey || ciphertext.length() != 1088 ||
      secret.length() != 32) return false;
  auto ctx = EVP_PKEY_CTX_new_from_pkey(nullptr, key->pkey, nullptr);
  if (!ctx) return false;
  size_t n = secret.length();
  bool ok = EVP_PKEY_decapsulate_init(ctx, nullptr) == 1 &&
    EVP_PKEY_decapsulate(ctx, secret.data(), &n,
      ciphertext.data(), ciphertext.length()) == 1 && n == secret.length();
  EVP_PKEY_CTX_free(ctx);
  return ok;
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

bool pkey_csr(const PKey *key, void *ctx, CSRFn fn)
{
  if (!key || !key->pkey || !fn || !pkey_check_(key->pkey, true))
    return false;

  const EVP_MD *md = nullptr;
  switch (EVP_PKEY_base_id(key->pkey)) {
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
    X509_REQ_set_pubkey(req, key->pkey) == 1 &&
    X509_REQ_sign(req, key->pkey, md) > 0 &&
    (len = i2d_X509_REQ(req, &der)) > 0 &&
    fn(ctx, ZuBSpan{der, static_cast<unsigned>(len)});
  OPENSSL_free(der);
  X509_NAME_free(subject);
  X509_REQ_free(req);
  return ok;
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

static X509 *x509_der_(ZuBSpan data)
{
  if (!data) return nullptr;
  const unsigned char *ptr = data.data();
  const unsigned char *end = ptr + data.length();
  X509 *cert = d2i_X509(nullptr, &ptr, data.length());
  if (cert && ptr != end) {
    X509_free(cert);
    cert = nullptr;
  }
  return cert;
}

static bool cert_store_add_(CertStore *store, X509 *cert)
{
  int ok = X509_STORE_add_cert(store->store, cert);
  if (ok != 1) {
    unsigned long err = ERR_peek_last_error();
    if (ERR_GET_LIB(err) == ERR_LIB_X509 &&
	ERR_GET_REASON(err) == X509_R_CERT_ALREADY_IN_HASH_TABLE) {
      ERR_clear_error();
      ok = 1;
    }
  }
  return ok == 1;
}

bool cert_store_add_der(CertStore *store, ZuBSpan data)
{
  if (!store || !store->store) return false;
  X509 *cert = x509_der_(data);
  if (!cert) return false;
  bool ok = cert_store_add_(store, cert);
  X509_free(cert);
  return ok;
}

bool cert_store_add_ca(CertStore *store, ZuBSpan data)
{
  if (!store || !store->store) return false;
  X509 *cert = x509_der_(data);
  if (!cert) return false;
  bool ok = X509_cmp_current_time(X509_get0_notBefore(cert)) < 0 &&
    X509_cmp_current_time(X509_get0_notAfter(cert)) > 0 &&
    X509_check_ca(cert) > 0 && cert_store_add_(store, cert);
  X509_free(cert);
  return ok;
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

bool load_certificates_der(
  ptls_context_t *ctx, ZuSpan<const ZuBSpan> certs, const PKey *key)
{
  if (!ctx || !certs.length() || !key || !key->pkey ||
      !pkey_check_(key->pkey, true)) return false;

  STACK_OF(X509) *parsed = sk_X509_new_null();
  STACK_OF(X509) *untrusted = sk_X509_new_null();
  X509_STORE *store = X509_STORE_new();
  X509_STORE_CTX *verify = X509_STORE_CTX_new();
  ptls_iovec_t *list = nullptr;
  bool ok = parsed && untrusted && store && verify;
  unsigned count = certs.length();

  for (unsigned i = 0; ok && i < count; ++i) {
    X509 *cert = x509_der_(certs[i]);
    if (!cert || !sk_X509_push(parsed, cert)) {
      X509_free(cert);
      ok = false;
    }
  }

  X509 *leaf = ok ? sk_X509_value(parsed, 0) : nullptr;
  EVP_PKEY *pub = leaf ? X509_get_pubkey(leaf) : nullptr;
  ok = ok && pub && EVP_PKEY_eq(pub, key->pkey) == 1;
  EVP_PKEY_free(pub);

  X509 *anchor = ok ? sk_X509_value(parsed, count - 1) : nullptr;
  if (ok) ok = X509_STORE_add_cert(store, anchor) == 1;
  for (unsigned i = 1; ok && i + 1 < count; ++i)
    if (!sk_X509_push(untrusted, sk_X509_value(parsed, i))) ok = false;
  if (ok) ok = X509_STORE_CTX_init(verify, store, leaf, untrusted) == 1;
  if (ok) X509_STORE_CTX_set_flags(
    verify, X509_V_FLAG_PARTIAL_CHAIN | X509_V_FLAG_X509_STRICT);
  if (ok) ok = X509_STORE_CTX_set_purpose(
    verify, X509_PURPOSE_SSL_CLIENT) == 1;
  if (ok) ok = X509_verify_cert(verify) == 1;

  if (ok) {
    list = static_cast<ptls_iovec_t *>(
      calloc(count, sizeof(ptls_iovec_t)));
    ok = !!list;
  }
  for (unsigned i = 0; ok && i < count; ++i) {
    auto cert = certs[i];
    list[i].base = static_cast<uint8_t *>(malloc(cert.length()));
    if (!list[i].base) {
      ok = false;
      break;
    }
    memcpy(list[i].base, cert.data(), cert.length());
    list[i].len = cert.length();
  }
  if (ok) {
    if (ctx->certificates.list) {
      for (size_t i = 0; i < ctx->certificates.count; ++i)
	free(ctx->certificates.list[i].base);
      free(ctx->certificates.list);
    }
    ctx->certificates.list = list;
    ctx->certificates.count = count;
    list = nullptr;
  }
  if (list) {
    for (unsigned i = 0; i < count; ++i) free(list[i].base);
    free(list);
  }
  X509_STORE_CTX_free(verify);
  X509_STORE_free(store);
  sk_X509_free(untrusted);
  sk_X509_pop_free(parsed, X509_free);
  return ok;
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
