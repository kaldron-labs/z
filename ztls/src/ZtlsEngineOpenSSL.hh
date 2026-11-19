/*
 * Copyright (c) 2016 DeNA Co., Ltd., Kazuho Oku
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef ZtlsEngineOpenSSL_HH
#define ZtlsEngineOpenSSL_HH

#include <openssl/opensslv.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/x509.h>
#include <openssl/opensslconf.h>
#include <zlib/ZtlsEngine.hh>

#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(OPENSSL_NO_CHACHA) && !defined(OPENSSL_NO_POLY1305)
#define Ztls_OPENSSL_HAVE_CHACHA20_POLY1305 1
#else
#define Ztls_OPENSSL_HAVE_CHACHA20_POLY1305 0
#endif

#if OPENSSL_VERSION_NUMBER >= 0x10100010L && !defined(LIBRESSL_VERSION_NUMBER) && !defined(OPENSSL_NO_ASYNC)
#include <openssl/async.h>
#define Ztls_OPENSSL_HAVE_ASYNC 1
#else
#define Ztls_OPENSSL_HAVE_ASYNC 0
#endif

namespace Ztls_ {

extern key_exchange_algorithm_t openssl_secp256r1;
#ifdef NID_secp384r1
#define Ztls_OPENSSL_HAVE_SECP384R1 1
#define Ztls_OPENSSL_HAS_SECP384R1 1 /* deprecated; use HAVE_ */
extern key_exchange_algorithm_t openssl_secp384r1;
#endif
#ifdef NID_secp521r1
#define Ztls_OPENSSL_HAVE_SECP521R1 1
#define Ztls_OPENSSL_HAS_SECP521R1 1 /* deprecated; use HAVE_ */
extern key_exchange_algorithm_t openssl_secp521r1;
#endif
#ifdef EVP_PKEY_ED25519
#define Ztls_OPENSSL_HAVE_ED25519 1
#endif
#if defined(NID_X25519) && !defined(LIBRESSL_VERSION_NUMBER)
#define Ztls_OPENSSL_HAVE_X25519 1
#define Ztls_OPENSSL_HAS_X25519 1 /* deprecated; use HAVE_ */
extern key_exchange_algorithm_t openssl_x25519;
#else
#define Ztls_OPENSSL_HAVE_X25519 0
#define Ztls_OPENSSL_HAS_X25519 0 /* deprecated; use HAVE_ */
#endif

#ifdef OPENSSL_IS_BORINGSSL
/**
 * this flag is set when the various post-quantum key encapsulation algorithms are available; note however that, unlike the non-PQC
 * key exchange methods above that test availability using using macros, the availability of these PQ algorithms can tested only by
 * actually instantiating the key exchange objects
 */
#define Ztls_OPENSSL_HAVE_MLKEM 0
/**
 * this flag is set when either openssl or boringssl provides the algorithm
 */
#define Ztls_OPENSSL_HAVE_X25519MLKEM768 1
#elif OPENSSL_VERSION_NUMBER >= 0x30500000L
#define Ztls_OPENSSL_HAVE_MLKEM 1
#define Ztls_OPENSSL_HAVE_X25519MLKEM768 1
#else
#define Ztls_OPENSSL_HAVE_MLKEM 0
#define Ztls_OPENSSL_HAVE_X25519MLKEM768 0
#endif
#if Ztls_OPENSSL_HAVE_MLKEM
extern key_exchange_algorithm_t openssl_mlkem512;
extern key_exchange_algorithm_t openssl_mlkem768;
extern key_exchange_algorithm_t openssl_mlkem1024;
extern key_exchange_algorithm_t openssl_secp256r1mlkem768;
extern key_exchange_algorithm_t openssl_secp384r1mlkem1024;
#endif
#if Ztls_OPENSSL_HAVE_X25519MLKEM768
extern key_exchange_algorithm_t openssl_x25519mlkem768;
#endif

/* when boringssl is used, existence of libdecrepit is assumed */
#if !defined(OPENSSL_NO_BF) || defined(OPENSSL_IS_BORINGSSL)
#define Ztls_OPENSSL_HAVE_BF 1
#else
#define Ztls_OPENSSL_HAVE_BF 0
#endif

ZtlsExtern key_exchange_algorithm_t *openssl_key_exchanges[];
ZtlsExtern key_exchange_algorithm_t *openssl_key_exchanges_all[];

extern cipher_algorithm_t openssl_aes128ecb;
extern cipher_algorithm_t openssl_aes128ctr;
ZtlsExtern aead_algorithm_t openssl_aes128gcm;
extern cipher_algorithm_t openssl_aes256ecb;
extern cipher_algorithm_t openssl_aes256ctr;
ZtlsExtern aead_algorithm_t openssl_aes256gcm;
ZtlsExtern hash_algorithm_t openssl_sha256;
ZtlsExtern hash_algorithm_t openssl_sha384;
extern hash_algorithm_t openssl_sha512;
ZtlsExtern cipher_suite_t openssl_aes128gcmsha256;
extern cipher_suite_t openssl_aes256gcmsha384;
ZtlsExtern cipher_suite_t *openssl_cipher_suites[];
extern cipher_suite_t *openssl_cipher_suites_all[];

#if Ztls_OPENSSL_HAVE_CHACHA20_POLY1305
extern cipher_algorithm_t openssl_chacha20;
ZtlsExtern aead_algorithm_t openssl_chacha20poly1305;
extern cipher_suite_t openssl_chacha20poly1305sha256;
#endif

#ifdef Ztls_HAVE_AEGIS
extern aead_algorithm_t openssl_aegis128l;
extern aead_algorithm_t openssl_aegis256;
extern cipher_suite_t openssl_aegis128lsha256;
extern cipher_suite_t openssl_aegis256sha512;
#endif


#if Ztls_OPENSSL_HAVE_BF
extern cipher_algorithm_t openssl_bfecb;
#endif


extern hpke_kem_t openssl_hpke_kem_p256sha256;
extern hpke_kem_t openssl_hpke_kem_p384sha384;
#if Ztls_OPENSSL_HAVE_X25519
extern hpke_kem_t openssl_hpke_kem_x25519sha256;
#endif
extern hpke_kem_t *openssl_hpke_kems[];

extern hpke_cipher_suite_t openssl_hpke_aes128gcmsha256;
extern hpke_cipher_suite_t openssl_hpke_aes128gcmsha512;
extern hpke_cipher_suite_t openssl_hpke_aes256gcmsha384;
#if Ztls_OPENSSL_HAVE_CHACHA20_POLY1305
extern hpke_cipher_suite_t openssl_hpke_chacha20poly1305sha256;
#endif
extern hpke_cipher_suite_t *openssl_hpke_cipher_suites[];

ZtlsAPI void openssl_random_bytes(void *buf, size_t len);
/**
 * constructs a key exchange context. pkey's reference count is incremented.
 */
int openssl_create_key_exchange(key_exchange_context_t **ctx, EVP_PKEY *pkey);

typedef struct st_openssl_signature_scheme_t {
    uint16_t scheme_id;
    const EVP_MD *(*scheme_md)(void);
} openssl_signature_scheme_t;

/**
 * Given a private key, returns a list of compatible signature schemes. This list is terminated by scheme_id of UINT16_MAX.
 */
const openssl_signature_scheme_t *openssl_lookup_signature_schemes(EVP_PKEY *key);
/**
 * Given available schemes and input, choses one, or returns NULL if none is available.
 */
const openssl_signature_scheme_t *openssl_select_signature_scheme(const openssl_signature_scheme_t *available,
                                                                            const uint16_t *algorithms, size_t num_algorithms);

typedef struct st_openssl_sign_certificate_t {
    sign_certificate_t super;
    EVP_PKEY *key;
    const openssl_signature_scheme_t *schemes; /* terminated by .scheme_id == UINT16_MAX */
    /**
     * When set to true, indicates to the backend that the signature can be generated asynchronously. When the backend decides to
     * generate the signature asynchronously, `handshake` will return Ztls_ERROR_ASYNC_OPERATION. When receiving that error
     * code, the connection exposes the file descriptor for its pending signature. The owner resumes the handshake or retires
     * the operation when the descriptor becomes readable.
     */
    unsigned async : 1;
} openssl_sign_certificate_t;

ZtlsAPI int openssl_init_sign_certificate(openssl_sign_certificate_t *self, EVP_PKEY *key);
ZtlsAPI void openssl_dispose_sign_certificate(openssl_sign_certificate_t *self);

typedef struct st_openssl_raw_pubkey_verify_certificate_t {
    verify_certificate_t super;
    EVP_PKEY *expected_pubkey;
} openssl_raw_pubkey_verify_certificate_t;

/**
 * When verifying the certificate chain, this optional callback can be used to store necessary information (e.g., client certificate
 * chain being provided) or to override the result.
 * To give an example, when `context_t::require_client_authentication` is set but the client does not provide a certificate,
 * the default behavior of the verifier is to reject the handshake. That can be overridden by supplying an overriding callback that
 * returns `0` (i.e., success) under such condition (as indicated by `ret == Ztls_ALERT_CERTIFICATE_REQUIRED`).
 */
Ztls_CALLBACK_TYPE(int, openssl_override_verify_certificate, Ztls::Cxn *tls, int ret, int ossl_ret, X509 *cert,
                   STACK_OF(X509) * chain);

typedef struct st_openssl_verify_certificate_t {
    verify_certificate_t super;
    X509_STORE *cert_store;
    openssl_override_verify_certificate_t *override_callback;
} openssl_verify_certificate_t;

ZtlsAPI int openssl_init_verify_certificate(openssl_verify_certificate_t *self, X509_STORE *store);
ZtlsAPI void openssl_dispose_verify_certificate(openssl_verify_certificate_t *self);
X509_STORE *openssl_create_default_certificate_store(void);

int openssl_raw_pubkey_init_verify_certificate(openssl_raw_pubkey_verify_certificate_t *self, EVP_PKEY *pubkey);
void openssl_raw_pubkey_dispose_verify_certificate(openssl_raw_pubkey_verify_certificate_t *self);

int openssl_encrypt_ticket(buffer_t *dst, Bytes src,
                                int (*cb)(unsigned char *, unsigned char *, EVP_CIPHER_CTX *, HMAC_CTX *, int));
int openssl_decrypt_ticket(buffer_t *dst, Bytes src,
                                int (*cb)(unsigned char *, unsigned char *, EVP_CIPHER_CTX *, HMAC_CTX *, int));

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
ZtlsAPI int openssl_encrypt_ticket_evp(buffer_t *dst, Bytes src,
                                    int (*cb)(unsigned char *, unsigned char *, EVP_CIPHER_CTX *, EVP_MAC_CTX *, int));
ZtlsAPI int openssl_decrypt_ticket_evp(buffer_t *dst, Bytes src,
                                    int (*cb)(unsigned char *, unsigned char *, EVP_CIPHER_CTX *, EVP_MAC_CTX *, int));
#endif

} // Ztls_

#endif
