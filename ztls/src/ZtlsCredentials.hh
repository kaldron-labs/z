//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TLS credentials, certificate stores, and session ticket keys

#ifndef ZtlsCredentials_HH
#define ZtlsCredentials_HH

#include <zlib/ZtlsEngine.hh>
#include <zlib/ZtlsPKOps.hh>

namespace Ztls {

struct CertStore;
struct SignCert;
struct VerifyCert;
struct TicketKey;

ZtlsAPI CertStore *cert_store_new();
ZtlsAPI void cert_store_free(CertStore *);
ZtlsAPI bool cert_store_load_path(CertStore *, const char *path);
ZtlsAPI bool cert_store_load_file(CertStore *, const char *path);
ZtlsAPI bool cert_store_add_der(CertStore *, ZuBSpan cert);
ZtlsAPI bool cert_store_add_ca(CertStore *, ZuBSpan cert);

ZtlsAPI VerifyCert *verify_cert_new(CertStore *);
ZtlsAPI void verify_cert_free(VerifyCert *);
ZtlsAPI Ztls_::verify_certificate_t *verify_cert_cb(VerifyCert *);

ZtlsAPI SignCert *sign_cert_new(const PK::Key &);
ZtlsAPI void sign_cert_free(SignCert *);
ZtlsAPI Ztls_::sign_certificate_t *sign_cert_cb(SignCert *);
ZtlsAPI bool sign_cert_async(SignCert *, bool);

ZtlsAPI bool load_certificates(Ztls_::context_t *ctx, const char *path);
ZtlsAPI bool load_certificates_der(
  Ztls_::context_t *, ZuSpan<const ZuBSpan> certs, const PK::Key &key);

ZtlsAPI TicketKey *ticket_key_new();
ZtlsAPI void ticket_key_free(TicketKey *);
ZtlsAPI Ztls_::encrypt_ticket_t *ticket_encrypt_cb(TicketKey *);

} // namespace Ztls

#endif /* ZtlsCredentials_HH */
