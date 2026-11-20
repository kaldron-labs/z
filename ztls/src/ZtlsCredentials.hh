//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TLS credentials, certificate stores, and session ticket keys

#ifndef ZtlsCredentials_HH
#define ZtlsCredentials_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZtlsEngine.hh>
#include <zlib/ZtlsPKOps.hh>

namespace Ztls {

struct CertStore;
struct SignCert;
struct VerifyCert;
struct TicketKey;

Ztls_API CertStore *cert_store_new();
Ztls_API void cert_store_free(CertStore *);
Ztls_API bool cert_store_load_path(CertStore *, const char *path);
Ztls_API bool cert_store_load_file(CertStore *, const char *path);
Ztls_API bool cert_store_add_der(CertStore *, ZuBSpan cert);
Ztls_API bool cert_store_add_ca(CertStore *, ZuBSpan cert);

Ztls_API VerifyCert *verify_cert_new(CertStore *);
Ztls_API void verify_cert_free(VerifyCert *);
Ztls_API Ztls_::VerifyCertificate *verify_cert_cb(VerifyCert *);

Ztls_API SignCert *sign_cert_new(const PK::Key &);
Ztls_API void sign_cert_free(SignCert *);
Ztls_API Ztls_::SignCertificate *sign_cert_cb(SignCert *);
Ztls_API bool sign_cert_async(SignCert *, bool);

Ztls_API bool load_certificates(Ztls_::Context *ctx, const char *path);
Ztls_API bool
load_certificates_der(Ztls_::Context *, ZuSpan<const ZuBSpan> certs, const PK::Key &key);

Ztls_API TicketKey *ticket_key_new();
Ztls_API void ticket_key_free(TicketKey *);
Ztls_API Ztls_::EncryptTicket *ticket_encrypt_cb(TicketKey *);

} // namespace Ztls

// ZtlsCredentials_HH
#endif
