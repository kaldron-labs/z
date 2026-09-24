//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// backend facade for the zpicotls back-end
// - currently OpenSSL
// - minicrypto could be a future option

#ifndef ZtlsBackend_HH
#define ZtlsBackend_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#ifndef Ztls_OpenSSL
#define Ztls_OpenSSL 1
#endif

#ifndef Ztls_Fusion
#if defined(__x86_64__) || defined(_M_X64) || \
    defined(__i386__) || defined(_M_IX86)
#define Ztls_Fusion 1
#else
#define Ztls_Fusion 0
#endif
#endif

#if Ztls_OpenSSL
#include <openssl/crypto.h>
#define Ztls_memcmp CRYPTO_memcmp
#endif

#include <zpicotls.h>
#if Ztls_Fusion
#include <zpicotls/fusion.h>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZtEnum.hh>

template <>
struct ZuTraits<ptls_iovec_t> : public ZuBaseTraits<ptls_iovec_t> {
  enum { IsArray = 1, IsSpan = 1, IsPrimitive = 0 };
  using Elem = uint8_t;

  static uint8_t *data(ptls_iovec_t &v) { return v.base; }
  static const uint8_t *data(const ptls_iovec_t &v) { return v.base; }
  static size_t length(const ptls_iovec_t &v) { return v.len; }
};

namespace Ztls {

ZtEnumNS(ZtlsAPI, MDAlg, int8_t,
  SHA1,
  SHA256,
  SHA384,
  SHA512);

using MDType = MDAlg::T;
inline constexpr MDType SHA1 = MDAlg::SHA1;
inline constexpr MDType SHA256 = MDAlg::SHA256;
inline constexpr MDType SHA384 = MDAlg::SHA384;
inline constexpr MDType SHA512 = MDAlg::SHA512;

namespace Backend {

struct PKey;
struct CertStore;
struct SignCert;
struct VerifyCert;
struct TicketKey;

ZtlsAPI bool init();
ZtlsAPI bool random_bytes(ZuSpan<uint8_t> data);

using RandomBytesFn = void (*)(void *, size_t);
ZtlsAPI RandomBytesFn random_bytes_cb();
ZtlsAPI ptls_key_exchange_algorithm_t **key_exchanges();
ZtlsAPI ptls_cipher_suite_t **cipher_suites();
ZtlsAPI ptls_cipher_suite_t *cipher_suite(uint16_t id);
ZtlsAPI ptls_cipher_suite_t *tls12_ecdhe_rsa_aes128gcmsha256();
ZtlsAPI ptls_cipher_suite_t *tls12_ecdhe_rsa_chacha20poly1305sha256();

ZtlsAPI ptls_hash_algorithm_t *hash_algorithm(MDType type);
ZtlsAPI size_t hash_size(MDType type);

ZtlsAPI size_t format_error(int err, char *buf, size_t len);

ZtlsAPI PKey *pkey_new();
ZtlsAPI void pkey_free(PKey *);

ZtlsAPI size_t pkey_rsa_size(const PKey *);
ZtlsAPI bool pkey_rsa_generate(PKey *, unsigned bits);
ZtlsAPI bool pkey_rsa_import_public(PKey *, ZuBSpan modulus, ZuBSpan pubExp);
ZtlsAPI bool pkey_rsa_import_private(
  PKey *, ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp,
  ZuBSpan prime1, ZuBSpan prime2, ZuBSpan exp1, ZuBSpan exp2, ZuBSpan coeff);
ZtlsAPI bool pkey_rsa_export_public(
  const PKey *, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp);
ZtlsAPI bool pkey_rsa_export_private(
  const PKey *, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp,
  ZuSpan<uint8_t> prvExp, ZuSpan<uint8_t> prime1, ZuSpan<uint8_t> prime2,
  ZuSpan<uint8_t> exp1, ZuSpan<uint8_t> exp2, ZuSpan<uint8_t> coeff);

ZtlsAPI size_t pkey_ec_key_size(const PKey *);
ZtlsAPI size_t pkey_ec_public_size(const PKey *);
ZtlsAPI size_t pkey_ec_oid_size(const PKey *);
ZtlsAPI bool pkey_ec_generate(PKey *, ZuBSpan oid);
ZtlsAPI bool pkey_ec_import_public(PKey *, ZuBSpan oid, ZuBSpan pubKey);
ZtlsAPI bool pkey_ec_import_private(PKey *, ZuBSpan oid, ZuBSpan key);
ZtlsAPI bool pkey_ec_export_private(const PKey *, ZuSpan<uint8_t> key);
ZtlsAPI bool pkey_ec_export_public(const PKey *, ZuSpan<uint8_t> pubKey);
ZtlsAPI bool pkey_ec_export_oid(const PKey *, ZuSpan<uint8_t> oid);

ZtlsAPI bool pkey_ed25519_generate(PKey *);
ZtlsAPI bool pkey_ed25519_import_public(PKey *, ZuBSpan pubKey);
ZtlsAPI bool pkey_ed25519_import_private(PKey *, ZuBSpan key);
ZtlsAPI bool pkey_ed25519_export_public(const PKey *, ZuSpan<uint8_t> pubKey);
ZtlsAPI bool pkey_ed25519_export_private(const PKey *, ZuSpan<uint8_t> key);

ZtlsAPI bool pkey_mlkem768_generate(PKey *);
ZtlsAPI bool pkey_mlkem768_import_seed(PKey *, ZuBSpan seed);
ZtlsAPI bool pkey_mlkem768_import_private(PKey *, ZuBSpan prvKey);
ZtlsAPI bool pkey_mlkem768_import_public(PKey *, ZuBSpan pubKey);
ZtlsAPI bool pkey_mlkem768_export_private(
  const PKey *, ZuSpan<uint8_t> prvKey, size_t *length);
ZtlsAPI bool pkey_mlkem768_export_public(
  const PKey *, ZuSpan<uint8_t> pubKey);
ZtlsAPI bool pkey_mlkem768_encapsulate(
  const PKey *, ZuSpan<uint8_t> ciphertext, ZuSpan<uint8_t> secret);
ZtlsAPI bool pkey_mlkem768_decapsulate(
  const PKey *, ZuBSpan ciphertext, ZuSpan<uint8_t> secret);

ZtlsAPI bool pkey_sign(const PKey *, MDType md, ZuBSpan data,
  ZuSpan<uint8_t> sig, size_t *siglen);
ZtlsAPI bool pkey_verify(const PKey *, MDType md, ZuBSpan data, ZuBSpan sig);
using CSRFn = bool (*)(void *, ZuBSpan);
ZtlsAPI bool pkey_csr(const PKey *, void *ctx, CSRFn);

ZtlsAPI CertStore *cert_store_new();
ZtlsAPI void cert_store_free(CertStore *);
ZtlsAPI bool cert_store_load_path(CertStore *, const char *path);
ZtlsAPI bool cert_store_load_file(CertStore *, const char *path);
ZtlsAPI bool cert_store_add_der(CertStore *, ZuBSpan cert);
ZtlsAPI bool cert_store_add_ca(CertStore *, ZuBSpan cert);

ZtlsAPI VerifyCert *verify_cert_new(CertStore *);
ZtlsAPI void verify_cert_free(VerifyCert *);
ZtlsAPI ptls_verify_certificate_t *verify_cert_cb(VerifyCert *);

ZtlsAPI SignCert *sign_cert_new(PKey *);
ZtlsAPI void sign_cert_free(SignCert *);
ZtlsAPI ptls_sign_certificate_t *sign_cert_cb(SignCert *);
ZtlsAPI bool sign_cert_async(SignCert *, bool);

ZtlsAPI PKey *pkey_load_pem(const char *path);
ZtlsAPI bool load_certificates(ptls_context_t *ctx, const char *path);
ZtlsAPI bool load_certificates_der(
  ptls_context_t *, ZuSpan<const ZuBSpan> certs, const PKey *key);

ZtlsAPI TicketKey *ticket_key_new();
ZtlsAPI void ticket_key_free(TicketKey *);
ZtlsAPI ptls_encrypt_ticket_t *ticket_encrypt_cb(TicketKey *);

} // Backend

} // Ztls

#endif /* ZtlsBackend_HH */
