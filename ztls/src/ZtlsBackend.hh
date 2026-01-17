//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// backend facade for ztls (picotls/OpenSSL)

#ifndef ZtlsBackend_HH
#define ZtlsBackend_HH

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

#include <picotls.h>
#if Ztls_OpenSSL
#include <picotls/openssl.h>
#endif
#if Ztls_Fusion
#include <picotls/fusion.h>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZtEnum.hh>

namespace Ztls {

namespace MD {
  ZtEnum(MD, int8_t,
    SHA1,
    SHA256,
    SHA384,
    SHA512);
}

namespace Backend {

struct PKey;
struct CertStore;
struct SignCert;
struct VerifyCert;
struct TicketKey;

bool init();
bool random_bytes(ZuSpan<uint8_t> data);

ptls_hash_algorithm_t *hash_algorithm(MDType type);
size_t hash_size(MDType type);

size_t format_error(int err, char *buf, size_t len);

PKey *pkey_new();
void pkey_free(PKey *);

size_t pkey_rsa_size(const PKey *);
bool pkey_rsa_generate(PKey *, unsigned bits);
bool pkey_rsa_import_public(PKey *, ZuBSpan modulus, ZuBSpan pubExp);
bool pkey_rsa_import_private(
  PKey *, ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp,
  ZuBSpan prime1, ZuBSpan prime2, ZuBSpan exp1, ZuBSpan exp2, ZuBSpan coeff);
bool pkey_rsa_export_public(
  const PKey *, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp);
bool pkey_rsa_export_private(
  const PKey *, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp,
  ZuSpan<uint8_t> prvExp, ZuSpan<uint8_t> prime1, ZuSpan<uint8_t> prime2,
  ZuSpan<uint8_t> exp1, ZuSpan<uint8_t> exp2, ZuSpan<uint8_t> coeff);

size_t pkey_ec_key_size(const PKey *);
size_t pkey_ec_public_size(const PKey *);
size_t pkey_ec_oid_size(const PKey *);
bool pkey_ec_generate(PKey *, ZuBSpan oid);
bool pkey_ec_import_public(PKey *, ZuBSpan oid, ZuBSpan pubKey);
bool pkey_ec_import_private(PKey *, ZuBSpan oid, ZuBSpan key);
bool pkey_ec_export_private(const PKey *, ZuSpan<uint8_t> key);
bool pkey_ec_export_public(const PKey *, ZuSpan<uint8_t> pubKey);
bool pkey_ec_export_oid(const PKey *, ZuSpan<uint8_t> oid);

bool pkey_sign(const PKey *, MDType md, ZuBSpan data,
  ZuSpan<uint8_t> sig, size_t *siglen);
bool pkey_verify(const PKey *, MDType md, ZuBSpan data, ZuBSpan sig);

CertStore *cert_store_new();
void cert_store_free(CertStore *);
bool cert_store_load_path(CertStore *, const char *path);
bool cert_store_load_file(CertStore *, const char *path);
bool cert_store_add_der(CertStore *, const uint8_t *data, size_t len);

VerifyCert *verify_cert_new(CertStore *);
void verify_cert_free(VerifyCert *);
ptls_verify_certificate_t *verify_cert_cb(VerifyCert *);

SignCert *sign_cert_new(PKey *);
void sign_cert_free(SignCert *);
ptls_sign_certificate_t *sign_cert_cb(SignCert *);

PKey *pkey_load_pem(const char *path);
bool load_certificates(ptls_context_t *ctx, const char *path);

TicketKey *ticket_key_new();
void ticket_key_free(TicketKey *);
ptls_encrypt_ticket_t *ticket_encrypt_cb(TicketKey *);

} // Backend

} // Ztls

#endif /* ZtlsBackend_HH */
