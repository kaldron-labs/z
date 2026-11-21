//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Copyright (c) 2016 DeNA Co., Ltd., Kazuho Oku
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

#ifndef ZtlsEngineOpenSSL_HH
#define ZtlsEngineOpenSSL_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <openssl/async.h>
#include <openssl/opensslv.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/x509.h>
#include <openssl/opensslconf.h>

#include <zlib/ZtlsEngine.hh>

#ifndef ZtlsOpenSSLHaveChaCha
#if !defined(OPENSSL_NO_CHACHA) && !defined(OPENSSL_NO_POLY1305)
#define ZtlsOpenSSLHaveChaCha 1
#else
#define ZtlsOpenSSLHaveChaCha 0
#endif
#endif

#ifndef ZtlsOpenSSLHaveAsync
#if !defined(OPENSSL_NO_ASYNC)
#define ZtlsOpenSSLHaveAsync 1
#else
#define ZtlsOpenSSLHaveAsync 0
#endif
#endif

#ifndef ZtlsOpenSSLHaveSecp384r1
#if defined(NID_secp384r1)
#define ZtlsOpenSSLHaveSecp384r1 1
#else
#define ZtlsOpenSSLHaveSecp384r1 0
#endif
#endif

#ifndef ZtlsOpenSSLHaveSecp521r1
#if defined(NID_secp521r1)
#define ZtlsOpenSSLHaveSecp521r1 1
#else
#define ZtlsOpenSSLHaveSecp521r1 0
#endif
#endif

#ifndef ZtlsOpenSSLHaveEd25519
#if defined(EVP_PKEY_ED25519)
#define ZtlsOpenSSLHaveEd25519 1
#else
#define ZtlsOpenSSLHaveEd25519 0
#endif
#endif

#ifndef ZtlsOpenSSLHaveX25519
#if defined(NID_X25519)
#define ZtlsOpenSSLHaveX25519 1
#else
#define ZtlsOpenSSLHaveX25519 0
#endif
#endif

#ifndef ZtlsOpenSSLHaveBF
#if !defined(OPENSSL_NO_BF)
#define ZtlsOpenSSLHaveBF 1
#else
#define ZtlsOpenSSLHaveBF 0
#endif
#endif

namespace Ztls_ {

Ztls_Extern const KeyExchangeAlgo	&opensslSecp256r1;
#if ZtlsOpenSSLHaveSecp384r1
Ztls_Extern const KeyExchangeAlgo	&opensslSecp384r1;
#endif
#if ZtlsOpenSSLHaveSecp521r1
Ztls_Extern const KeyExchangeAlgo	&opensslSecp521r1;
#endif
#if ZtlsOpenSSLHaveX25519
Ztls_Extern const KeyExchangeAlgo	&opensslX25519;
#endif

Ztls_Extern const KeyExchangeAlgo	&opensslMlkem512;
Ztls_Extern const KeyExchangeAlgo	&opensslMLKEM768;
Ztls_Extern const KeyExchangeAlgo	&opensslMlkem1024;
Ztls_Extern const KeyExchangeAlgo	&opensslSecp256r1mlkem768;
Ztls_Extern const KeyExchangeAlgo	&opensslSecp384r1mlkem1024;
Ztls_Extern const KeyExchangeAlgo	&opensslX25519mlkem768;

Ztls_Extern const KeyExchangeAlgo	*opensslKeyExchanges[];
Ztls_Extern const KeyExchangeAlgo	*opensslKeyExchangesAll[];

Ztls_Extern const CipherAlgo		&opensslAes128ecb;
Ztls_Extern const CipherAlgo		&opensslAes128ctr;
Ztls_Extern const AeadAlgo		&opensslAes128gcm;
Ztls_Extern const CipherAlgo		&opensslAes256ecb;
Ztls_Extern const CipherAlgo		&opensslAes256ctr;
Ztls_Extern const AeadAlgo		&opensslAes256gcm;
Ztls_Extern const HashAlgo		&opensslSha256;
Ztls_Extern const HashAlgo		&opensslSha384;
Ztls_Extern const HashAlgo		&opensslSha512;
Ztls_Extern const CipherSuite		opensslAes128gcmsha256;
Ztls_Extern const CipherSuite		opensslAes256gcmsha384;
Ztls_Extern const CipherSuites		opensslCipherSuites;
Ztls_Extern const CipherSuites		opensslCipherSuitesAll;

#if ZtlsOpenSSLHaveChaCha
Ztls_Extern const CipherAlgo		&opensslChacha20;
Ztls_Extern const AeadAlgo		&opensslChacha20poly1305;
Ztls_Extern const CipherSuite		opensslChachasha256;
#endif

#if ZtlsOpenSSLHaveBF
Ztls_Extern const CipherAlgo		&opensslBfecb;
#endif

Ztls_Extern const HpkeKem		opensslHPKEKemP256SHA256;
#if ZtlsOpenSSLHaveSecp384r1
Ztls_Extern const HpkeKem		opensslHPKEKemP384SHA384;
#endif
#if ZtlsOpenSSLHaveX25519
Ztls_Extern const HpkeKem		opensslHPKEX25519sha256;
#endif
Ztls_Extern const HpkeKem		*opensslHPKEKems[];

Ztls_Extern const HpkeSuite		opensslHPKEAes128gcmsha256;
Ztls_Extern const HpkeSuite		opensslHPKEAes128gcmsha512;
Ztls_Extern const HpkeSuite		opensslHPKEAes256gcmsha384;
#if ZtlsOpenSSLHaveChaCha
Ztls_Extern const HpkeSuite		opensslHPKEChachasha256;
#endif
Ztls_Extern const HpkeSuites		opensslHPKECipherSuites;

Ztls_Extern RandomSource		&opensslRandom;

// constructs a key exchange context. pkey's reference count is incremented.
Ztls_Extern int opensslCreateKeyExchange(KeyExchangeContext **ctx, EVP_PKEY *pkey);

struct OpenSSLSigScheme {
  uint16_t	schemeId;

  explicit OpenSSLSigScheme(uint16_t id) : schemeId{id} { }
  virtual ~OpenSSLSigScheme() = default;
  virtual const EVP_MD *md() const = 0;
};

// Given a private key, returns a list of compatible signature schemes.
Ztls_Extern ZuSpan<const OpenSSLSigScheme *const> opensslLookupSigSchemes(EVP_PKEY *key);

// Given available schemes and input, choses one, or returns NULL if none is available.
Ztls_Extern const OpenSSLSigScheme *opensslSelectSigScheme(
  ZuSpan<const OpenSSLSigScheme *const> available, ZuSpan<const uint16_t> algorithms);

struct Ztls_API OpenSSLSignCert : SignCertificate {
  EVP_PKEY				*key = nullptr;
  ZuSpan<const OpenSSLSigScheme *const>	schemes;

  // When set to true, indicates to the backend that the signature can be generated
  // asynchronously. When the backend decides to generate the signature asynchronously,
  // `handshake` will return Ztls_ErrorAsyncOperation. When receiving that error code, the
  // connection exposes the file descriptor for its pending signature. The owner resumes the
  // handshake or retires the operation when the descriptor becomes readable.
  unsigned				async:1 = 0;

  OpenSSLSignCert() = default;
  OpenSSLSignCert(const OpenSSLSignCert &) = delete;
  OpenSSLSignCert &operator=(const OpenSSLSignCert &) = delete;
  ~OpenSSLSignCert() override;
  int call(
    Ztls::Cxn *tls, bool allowAsync, uint16_t *algorithm, Buffer *output, Bytes input,
    ZuSpan<const uint16_t> algorithms) override;
};

Ztls_Extern int opensslInitSignCert(OpenSSLSignCert *self, EVP_PKEY *key);
Ztls_Extern void opensslDisposeSignCert(OpenSSLSignCert *self);

// When verifying the certificate chain, this optional callback can be used to store necessary
// information (e.g., client certificate chain being provided) or to override the result. To give
// an example, when `Context::requireCliAuth` is set but the client does not provide a
// certificate, the default behavior of the verifier is to reject the handshake. That can be
// overridden by supplying an overriding callback that returns `0` (i.e., success) under such
// condition (as indicated by `ret == Ztls_AlertCertRequired`).
struct OpenSSLCertVerify {
  virtual ~OpenSSLCertVerify() = default;
  virtual int call(Ztls::Cxn *tls, int ret, int osslRet, X509 *cert, STACK_OF(X509) * chain) = 0;
};

struct Ztls_API OpenSSLVerifyCert : VerifyCertificate {
  X509_STORE		*certStore = nullptr;
  OpenSSLCertVerify	*overrideCallback = nullptr;

  OpenSSLVerifyCert() = default;
  OpenSSLVerifyCert(const OpenSSLVerifyCert &) = delete;
  OpenSSLVerifyCert &operator=(const OpenSSLVerifyCert &) = delete;
  ~OpenSSLVerifyCert() override;
  int call(
    Ztls::Cxn *tls, const char *serverName, VerifySignature **signature,
    ZuSpan<const Bytes> certs) override;
};

Ztls_Extern int opensslInitVerifyCert(OpenSSLVerifyCert *self, X509_STORE *store);
Ztls_Extern void opensslDisposeVerifyCert(OpenSSLVerifyCert *self);
Ztls_Extern X509_STORE *opensslDefaultCertStore(void);

Ztls_Extern int opensslEncryptTicketEvp(
  Buffer *dst, Bytes src,
  int (*cb)(unsigned char *, unsigned char *, EVP_CIPHER_CTX *, EVP_MAC_CTX *, int));
Ztls_Extern int opensslDecryptTicketEvp(
  Buffer *dst, Bytes src,
  int (*cb)(unsigned char *, unsigned char *, EVP_CIPHER_CTX *, EVP_MAC_CTX *, int));

} // namespace Ztls_

#endif
