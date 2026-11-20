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

Ztls_Extern CertStore *certStoreNew();
Ztls_Extern void certStoreFree(CertStore *);
Ztls_Extern bool certStoreLoadPath(CertStore *, const char *path);
Ztls_Extern bool certStoreLoadFile(CertStore *, const char *path);
Ztls_Extern bool certStoreAddDER(CertStore *, ZuBSpan cert);
Ztls_Extern bool certStoreAddCA(CertStore *, ZuBSpan cert);

Ztls_Extern VerifyCert *verifyCertNew(CertStore *);
Ztls_Extern void verifyCertFree(VerifyCert *);
Ztls_Extern Ztls_::VerifyCertificate *verifyCertHook(VerifyCert *);

Ztls_Extern SignCert *signCertNew(const PK::Key &);
Ztls_Extern void signCertFree(SignCert *);
Ztls_Extern Ztls_::SignCertificate *signCertHook(SignCert *);
Ztls_Extern bool signCertAsync(SignCert *, bool);

Ztls_Extern bool loadCerts(Ztls_::Context *ctx, const char *path);
Ztls_Extern bool loadCertsDER(Ztls_::Context *, ZuSpan<const ZuBSpan> certs, const PK::Key &key);

Ztls_Extern TicketKey *ticketKeyNew();
Ztls_Extern void ticketKeyFree(TicketKey *);
Ztls_Extern Ztls_::EncryptTicket *ticketEncryptHook(TicketKey *);

} // namespace Ztls

// ZtlsCredentials_HH
#endif
