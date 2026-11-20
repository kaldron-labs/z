//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

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

Ztls_Extern const KeyExchangeAlgo &openssl_secp256r1;
#if ZtlsOpenSSLHaveSecp384r1
Ztls_Extern const KeyExchangeAlgo &openssl_secp384r1;
#endif
#if ZtlsOpenSSLHaveSecp521r1
Ztls_Extern const KeyExchangeAlgo &openssl_secp521r1;
#endif
#if ZtlsOpenSSLHaveX25519
Ztls_Extern const KeyExchangeAlgo &openssl_x25519;
#endif

Ztls_Extern const KeyExchangeAlgo &openssl_mlkem512;
Ztls_Extern const KeyExchangeAlgo &openssl_mlkem768;
Ztls_Extern const KeyExchangeAlgo &openssl_mlkem1024;
Ztls_Extern const KeyExchangeAlgo &openssl_secp256r1mlkem768;
Ztls_Extern const KeyExchangeAlgo &openssl_secp384r1mlkem1024;
Ztls_Extern const KeyExchangeAlgo &openssl_x25519mlkem768;

Ztls_Extern const KeyExchangeAlgo *openssl_key_exchanges[];
Ztls_Extern const KeyExchangeAlgo *openssl_key_exchanges_all[];

Ztls_Extern const CipherAlgo  &openssl_aes128ecb;
extern const CipherAlgo	      &openssl_aes128ctr;
Ztls_Extern const AeadAlgo    &openssl_aes128gcm;
extern const CipherAlgo	      &openssl_aes256ecb;
extern const CipherAlgo	      &openssl_aes256ctr;
Ztls_Extern const AeadAlgo    &openssl_aes256gcm;
Ztls_Extern const HashAlgo    &openssl_sha256;
Ztls_Extern const HashAlgo    &openssl_sha384;
Ztls_Extern const HashAlgo    &openssl_sha512;
Ztls_Extern const CipherSuite  openssl_aes128gcmsha256;
extern const CipherSuite       openssl_aes256gcmsha384;
Ztls_Extern const CipherSuites openssl_cipher_suites;
extern const CipherSuites      openssl_cipher_suites_all;

#if ZtlsOpenSSLHaveChaCha
extern const CipherAlgo	   &openssl_chacha20;
Ztls_Extern const AeadAlgo &openssl_chacha20poly1305;
extern const CipherSuite    openssl_chachasha256;
#endif

#if ZtlsOpenSSLHaveBF
extern const CipherAlgo &openssl_bfecb;
#endif

extern const HpkeKem openssl_hpke_kem_p256sha256;
#if ZtlsOpenSSLHaveSecp384r1
extern const HpkeKem openssl_hpke_kem_p384sha384;
#endif
#if ZtlsOpenSSLHaveX25519
Ztls_Extern const HpkeKem openssl_hpke_x25519sha256;
#endif
extern const HpkeKem *openssl_hpke_kems[];

Ztls_Extern const HpkeSuite openssl_hpke_aes128gcmsha256;
extern const HpkeSuite	    openssl_hpke_aes128gcmsha512;
extern const HpkeSuite	    openssl_hpke_aes256gcmsha384;
#if ZtlsOpenSSLHaveChaCha
extern const HpkeSuite openssl_hpke_chachasha256;
#endif
extern const HpkeSuite *openssl_hpke_cipher_suites[];

Ztls_Extern RandomSource &opensslRandom;

// constructs a key exchange context. pkey's reference count is incremented.
Ztls_API int openssl_create_key_exchange(KeyExchangeContext **ctx, EVP_PKEY *pkey);

struct OpenSSLSigScheme {
  uint16_t	schemeId;

  explicit OpenSSLSigScheme(uint16_t id) : schemeId{id} { }
  virtual ~OpenSSLSigScheme() = default;
  virtual const EVP_MD *md() const = 0;
};

// Given a private key, returns a list of compatible signature schemes.
ZuSpan<const OpenSSLSigScheme *const> openssl_lookup_sig_schemes(EVP_PKEY *key);

// Given available schemes and input, choses one, or returns NULL if none is available.
const OpenSSLSigScheme *openssl_select_sig_scheme(
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

Ztls_API int openssl_init_sign_cert(OpenSSLSignCert *self, EVP_PKEY *key);
Ztls_API void openssl_dispose_sign_cert(OpenSSLSignCert *self);

// When verifying the certificate chain, this optional callback can be used to store necessary
// information (e.g., client certificate chain being provided) or to override the result. To give
// an example, when `Context::requireCliAuth` is set but the client does not provide a
// certificate, the default behavior of the verifier is to reject the handshake. That can be
// overridden by supplying an overriding callback that returns `0` (i.e., success) under such
// condition (as indicated by `ret == Ztls_AlertCertRequired`).
struct OpenSSLCertVerify {
  virtual ~OpenSSLCertVerify() = default;
  virtual int call(Ztls::Cxn *tls, int ret, int ossl_ret, X509 *cert, STACK_OF(X509) * chain) = 0;
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

Ztls_API int openssl_init_verify_cert(OpenSSLVerifyCert *self, X509_STORE *store);
Ztls_API void openssl_dispose_verify_cert(OpenSSLVerifyCert *self);
X509_STORE *openssl_default_cert_store(void);

Ztls_API int openssl_encrypt_ticket_evp(
  Buffer *dst, Bytes src,
  int (*cb)(unsigned char *, unsigned char *, EVP_CIPHER_CTX *, EVP_MAC_CTX *, int));
Ztls_API int openssl_decrypt_ticket_evp(
  Buffer *dst, Bytes src,
  int (*cb)(unsigned char *, unsigned char *, EVP_CIPHER_CTX *, EVP_MAC_CTX *, int));

} // namespace Ztls_

#endif
