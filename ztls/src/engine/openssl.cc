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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define OPENSSL_API_COMPAT 0x00908000L
#include <openssl/bn.h>
#include <openssl/crypto.h>
#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/curve25519.h>
#include <openssl/chacha.h>
#include <openssl/poly1305.h>
#endif
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>
#include <zlib/ZmVHeap.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsCxn.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>
#ifdef OPENSSL_IS_BORINGSSL
#include "./chacha20poly1305.h"
#if defined(OPENSSL_IS_BORINGSSL) && Ztls_OPENSSL_HAVE_X25519MLKEM768
#include <openssl/mlkem.h>
#endif
#endif
#ifdef Ztls_HAVE_AEGIS
#include "./libaegis.h"
#endif

#include <zlib/ZtlsSec.hh>

namespace Ztls_ {

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#pragma warning(disable : 4996)
#endif

#if !defined(LIBRESSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10100000L
#define OPENSSL_1_1_API 1
#elif defined(LIBRESSL_VERSION_NUMBER) && LIBRESSL_VERSION_NUMBER >= 0x2070000fL
#define OPENSSL_1_1_API 1
#else
#define OPENSSL_1_1_API 0
#endif

#if !OPENSSL_1_1_API

#define EVP_PKEY_up_ref(p) CRYPTO_add(&(p)->references, 1, CRYPTO_LOCK_EVP_PKEY)
#define X509_STORE_up_ref(p) CRYPTO_add(&(p)->references, 1, CRYPTO_LOCK_X509_STORE)
#define X509_STORE_get0_param(p) ((p)->param)

static HMAC_CTX *HMAC_CTX_new(void)
{
    HMAC_CTX *ctx;

    if ((ctx = OPENSSL_malloc(sizeof(*ctx))) == NULL)
        return NULL;
    HMAC_CTX_init(ctx);
    return ctx;
}

static void HMAC_CTX_free(HMAC_CTX *ctx)
{
    HMAC_CTX_cleanup(ctx);
    OPENSSL_free(ctx);
}

static int EVP_CIPHER_CTX_reset(EVP_CIPHER_CTX *ctx)
{
    return EVP_CIPHER_CTX_cleanup(ctx);
}

#endif

static const openssl_signature_scheme_t rsa_signature_schemes[] = {{Ztls_SIGNATURE_RSA_PSS_RSAE_SHA256, EVP_sha256},
                                                                        {Ztls_SIGNATURE_RSA_PSS_RSAE_SHA384, EVP_sha384},
                                                                        {Ztls_SIGNATURE_RSA_PSS_RSAE_SHA512, EVP_sha512},
                                                                        {UINT16_MAX, NULL}};
static const openssl_signature_scheme_t secp256r1_signature_schemes[] = {{Ztls_SIGNATURE_ECDSA_SECP256R1_SHA256, EVP_sha256},
                                                                              {UINT16_MAX, NULL}};
#if Ztls_OPENSSL_HAVE_SECP384R1
static const openssl_signature_scheme_t secp384r1_signature_schemes[] = {{Ztls_SIGNATURE_ECDSA_SECP384R1_SHA384, EVP_sha384},
                                                                              {UINT16_MAX, NULL}};
#endif
#if Ztls_OPENSSL_HAVE_SECP521R1
static const openssl_signature_scheme_t secp521r1_signature_schemes[] = {{Ztls_SIGNATURE_ECDSA_SECP521R1_SHA512, EVP_sha512},
                                                                              {UINT16_MAX, NULL}};
#endif
#if Ztls_OPENSSL_HAVE_ED25519
static const openssl_signature_scheme_t ed25519_signature_schemes[] = {{Ztls_SIGNATURE_ED25519, NULL}, {UINT16_MAX, NULL}};
#endif

/**
 * The default list sent in ClientHello.signature_algorithms. ECDSA certificates are preferred.
 */
static const uint16_t default_signature_schemes[] = {
#if Ztls_OPENSSL_HAVE_ED25519
    Ztls_SIGNATURE_ED25519,
#endif
    Ztls_SIGNATURE_ECDSA_SECP256R1_SHA256,
#if Ztls_OPENSSL_HAVE_SECP384R1
    Ztls_SIGNATURE_ECDSA_SECP384R1_SHA384,
#endif
#if Ztls_OPENSSL_HAVE_SECP521R1
    Ztls_SIGNATURE_ECDSA_SECP521R1_SHA512,
#endif
    Ztls_SIGNATURE_RSA_PSS_RSAE_SHA512,
    Ztls_SIGNATURE_RSA_PSS_RSAE_SHA384,
    Ztls_SIGNATURE_RSA_PSS_RSAE_SHA256,
    UINT16_MAX};

const openssl_signature_scheme_t *openssl_lookup_signature_schemes(EVP_PKEY *key)
{
    const openssl_signature_scheme_t *schemes = NULL;

    switch (EVP_PKEY_id(key)) {
    case EVP_PKEY_RSA:
        schemes = rsa_signature_schemes;
        break;
    case EVP_PKEY_EC: {
        EC_KEY *eckey = EVP_PKEY_get1_EC_KEY(key);
        switch (EC_GROUP_get_curve_name(EC_KEY_get0_group(eckey))) {
        case NID_X9_62_prime256v1:
            schemes = secp256r1_signature_schemes;
            break;
#if Ztls_OPENSSL_HAVE_SECP384R1
        case NID_secp384r1:
            schemes = secp384r1_signature_schemes;
            break;
#endif
#if Ztls_OPENSSL_HAVE_SECP521R1
        case NID_secp521r1:
            schemes = secp521r1_signature_schemes;
            break;
#endif
        default:
            break;
        }
        EC_KEY_free(eckey);
    } break;
#if Ztls_OPENSSL_HAVE_ED25519
    case EVP_PKEY_ED25519:
        schemes = ed25519_signature_schemes;
        break;
#endif
    default:
        break;
    }

    return schemes;
}

const openssl_signature_scheme_t *openssl_select_signature_scheme(const openssl_signature_scheme_t *available,
                                                                            const uint16_t *algorithms, size_t num_algorithms)
{
    const openssl_signature_scheme_t *scheme;

    /* select the algorithm, driven by server-isde preference of `available` */
    for (scheme = available; scheme->scheme_id != UINT16_MAX; ++scheme)
        for (size_t i = 0; i != num_algorithms; ++i)
            if (algorithms[i] == scheme->scheme_id)
                return scheme;

    return NULL;
}

void openssl_random_bytes(void *buf, size_t len)
{
    Ztls::Random random;
    if (!random.random({static_cast<uint8_t *>(buf), len})) {
        ZiLOG(Fatal, "Ztls", "random byte generation failed");
        abort();
    }
}

static EC_KEY *ecdh_generate_key(EC_GROUP *group)
{
    EC_KEY *key;

    if ((key = EC_KEY_new()) == NULL)
        return NULL;
    if (!EC_KEY_set_group(key, group) || !EC_KEY_generate_key(key)) {
        EC_KEY_free(key);
        return NULL;
    }

    return key;
}

static int ecdh_calc_secret(KeyExchangeBytes *out, const EC_GROUP *group, EC_KEY *privkey, EC_POINT *peer_point)
{
    KeyExchangeBytes secret;
    secret.length((EC_GROUP_get_degree(group) + 7) / 8);
    if (ECDH_compute_key(secret.data(), secret.length(), peer_point, privkey, NULL) <= 0) {
        out->null();
        return Ztls_ALERT_HANDSHAKE_FAILURE;
    }
    *out = ZuMv(secret);
    return 0;
}

static EC_POINT *x9_62_decode_point(const EC_GROUP *group, Bytes vec, BN_CTX *bn_ctx)
{
    EC_POINT *point = NULL;

    if ((point = EC_POINT_new(group)) == NULL)
        return NULL;
    if (!EC_POINT_oct2point(group, point, vec.data(), vec.length(), bn_ctx)) {
        EC_POINT_free(point);
        return NULL;
    }

    return point;
}

static KeyExchangeBytes x9_62_encode_point(const EC_GROUP *group, const EC_POINT *point, BN_CTX *bn_ctx)
{
    auto length = EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED, NULL, 0, bn_ctx);
    if (!length)
        return {};
    KeyExchangeBytes point_bytes(length, length);
    if (EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED, point_bytes.data(), length, bn_ctx) != length)
        return {};
    return point_bytes;
}

struct st_x9_62_keyex_context_t {
    key_exchange_context_t super;
    BN_CTX *bn_ctx;
    EC_KEY *privkey;
    KeyExchangeBytes pubkey;
};
using X962Heap = ZmVHeap<"Ztls.Engine.ECKeyExchange", 64, 4096, alignof(st_x9_62_keyex_context_t)>;

static void x9_62_free_context(struct st_x9_62_keyex_context_t *ctx)
{
    EC_KEY_free(ctx->privkey);
    BN_CTX_free(ctx->bn_ctx);
    ctx->~st_x9_62_keyex_context_t();
    X962Heap::free(ctx, sizeof(st_x9_62_keyex_context_t));
}

static int x9_62_on_exchange(key_exchange_context_t **_ctx, int release, KeyExchangeBytes *secret, Bytes peerkey)
{
    struct st_x9_62_keyex_context_t *ctx = (struct st_x9_62_keyex_context_t *)*_ctx;
    const EC_GROUP *group = EC_KEY_get0_group(ctx->privkey);
    EC_POINT *peer_point = NULL;
    int ret;

    if (secret == NULL) {
        ret = 0;
        goto Exit;
    }

    if ((peer_point = x9_62_decode_point(group, peerkey, ctx->bn_ctx)) == NULL) {
        ret = Ztls_ALERT_DECODE_ERROR;
        goto Exit;
    }
    if ((ret = ecdh_calc_secret(secret, group, ctx->privkey, peer_point)) != 0)
        goto Exit;

Exit:
    if (peer_point != NULL)
        EC_POINT_free(peer_point);
    if (release) {
        x9_62_free_context(ctx);
        *_ctx = NULL;
    }
    return ret;
}

static int x9_62_create_context(key_exchange_algorithm_t *algo, struct st_x9_62_keyex_context_t **ctx)
{
    int ret;

    *ctx = new (X962Heap::alloc(sizeof(st_x9_62_keyex_context_t)))
        st_x9_62_keyex_context_t{{algo, {}, x9_62_on_exchange}};

    if (((*ctx)->bn_ctx = BN_CTX_new()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }

    ret = 0;
Exit:
    if (ret != 0 && *ctx != NULL) {
        x9_62_free_context(*ctx);
        *ctx = NULL;
    }
    return ret;
}

static int x9_62_setup_pubkey(struct st_x9_62_keyex_context_t *ctx)
{
    const EC_GROUP *group = EC_KEY_get0_group(ctx->privkey);
    const EC_POINT *pubkey = EC_KEY_get0_public_key(ctx->privkey);
    ctx->pubkey = x9_62_encode_point(group, pubkey, ctx->bn_ctx);
    if (!ctx->pubkey.length())
        return Ztls_ERROR_NO_MEMORY;
    ctx->super.pubkey = wire_bytes(ctx->pubkey.data(), ctx->pubkey.length());
    return 0;
}

static int x9_62_create_key_exchange(key_exchange_algorithm_t *algo, key_exchange_context_t **_ctx)
{
    EC_GROUP *group = NULL;
    struct st_x9_62_keyex_context_t *ctx = NULL;
    int ret;

    /* FIXME use a global? */
    if ((group = EC_GROUP_new_by_curve_name((int)algo->data)) == NULL) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if ((ret = x9_62_create_context(algo, &ctx)) != 0)
        goto Exit;
    if ((ctx->privkey = ecdh_generate_key(group)) == NULL) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if ((ret = x9_62_setup_pubkey(ctx)) != 0)
        goto Exit;
    ret = 0;

Exit:
    if (group != NULL)
        EC_GROUP_free(group);
    if (ret == 0) {
        *_ctx = &ctx->super;
    } else {
        if (ctx != NULL)
            x9_62_free_context(ctx);
        *_ctx = NULL;
    }

    return ret;
}

static int x9_62_init_key(key_exchange_algorithm_t *algo, key_exchange_context_t **_ctx, EC_KEY *eckey)
{
    struct st_x9_62_keyex_context_t *ctx = NULL;
    int ret;

    if ((ret = x9_62_create_context(algo, &ctx)) != 0)
        goto Exit;
    ctx->privkey = eckey;
    if ((ret = x9_62_setup_pubkey(ctx)) != 0)
        goto Exit;
    ret = 0;

Exit:
    if (ret == 0) {
        *_ctx = &ctx->super;
    } else {
        if (ctx != NULL)
            x9_62_free_context(ctx);
        *_ctx = NULL;
    }
    return ret;
}

static int x9_62_key_exchange(EC_GROUP *group, KeyExchangeBytes *pubkey, KeyExchangeBytes *secret, Bytes peerkey, BN_CTX *bn_ctx)
{
    EC_POINT *peer_point = NULL;
    EC_KEY *privkey = NULL;
    int ret;

    pubkey->null();
    secret->null();

    /* decode peer key */
    if ((peer_point = x9_62_decode_point(group, peerkey, bn_ctx)) == NULL) {
        ret = Ztls_ALERT_DECODE_ERROR;
        goto Exit;
    }

    /* create private key */
    if ((privkey = ecdh_generate_key(group)) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }

    /* encode public key */
    *pubkey = x9_62_encode_point(group, EC_KEY_get0_public_key(privkey), bn_ctx);
    if (!pubkey->length()) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }

    ret = ecdh_calc_secret(secret, group, privkey, peer_point);

Exit:
    if (peer_point != NULL)
        EC_POINT_free(peer_point);
    if (privkey != NULL)
        EC_KEY_free(privkey);
    if (ret != 0) {
        pubkey->null();
        secret->null();
    }
    return ret;
}

static int secp_key_exchange(key_exchange_algorithm_t *algo, KeyExchangeBytes *pubkey, KeyExchangeBytes *secret, Bytes peerkey)
{
    EC_GROUP *group = NULL;
    BN_CTX *bn_ctx = NULL;
    int ret;

    if ((group = EC_GROUP_new_by_curve_name((int)algo->data)) == NULL) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if ((bn_ctx = BN_CTX_new()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }

    ret = x9_62_key_exchange(group, pubkey, secret, peerkey, bn_ctx);

Exit:
    if (bn_ctx != NULL)
        BN_CTX_free(bn_ctx);
    if (group != NULL)
        EC_GROUP_free(group);
    return ret;
}

#if Ztls_OPENSSL_HAVE_X25519

struct st_evp_keyex_context_t {
    key_exchange_context_t super;
    EVP_PKEY *privkey;
};
using EvpKeyExchangeHeap = ZmVHeap<"Ztls.Engine.EVPKeyExchange", 64, 4096, alignof(st_evp_keyex_context_t)>;

static void evp_keyex_free(struct st_evp_keyex_context_t *ctx)
{
    if (ctx->privkey != NULL)
        EVP_PKEY_free(ctx->privkey);
    if (ctx->super.pubkey.data() != NULL)
        OPENSSL_free(ctx->super.pubkey.data());
    EvpKeyExchangeHeap::free(ctx, sizeof(st_evp_keyex_context_t));
}

static int evp_keyex_on_exchange(key_exchange_context_t **_ctx, int release, KeyExchangeBytes *secret, Bytes peerkey)
{
    struct st_evp_keyex_context_t *ctx = reinterpret_cast<struct st_evp_keyex_context_t *>(*_ctx);
    EVP_PKEY *evppeer = NULL;
    EVP_PKEY_CTX *evpctx = NULL;
    size_t length = 0;
    int ret;

    if (secret == NULL) {
        ret = 0;
        goto Exit;
    }

    secret->null();

    if (peerkey.length() != ctx->super.pubkey.length()) {
        ret = Ztls_ALERT_DECRYPT_ERROR;
        goto Exit;
    }

#ifdef OPENSSL_IS_BORINGSSL
#define X25519_KEY_SIZE 32
    if (ctx->super.algo->id == Ztls_GROUP_X25519) {
        secret->length(X25519_KEY_SIZE);
        /* fetch raw key and derive the secret */
        uint8_t sk_raw[X25519_KEY_SIZE];
        size_t sk_raw_len = sizeof(sk_raw);
        if (EVP_PKEY_get_raw_private_key(ctx->privkey, sk_raw, &sk_raw_len) != 1) {
            ZuClear(sk_raw, sizeof(sk_raw));
            ret = Ztls_ERROR_LIBRARY;
            goto Exit;
        }
        assert(sk_raw_len == sizeof(sk_raw));
        X25519(secret->data(), sk_raw, peerkey.data());
        ZuClear(sk_raw, sizeof(sk_raw));
        /* check bad key */
        static const uint8_t zeros[X25519_KEY_SIZE] = {0};
        if (Ztls::ctEqual({secret->data(), X25519_KEY_SIZE}, {zeros, X25519_KEY_SIZE})) {
            ret = Ztls_ERROR_INCOMPATIBLE_KEY;
            goto Exit;
        }
        /* success */
        ret = 0;
        goto Exit;
    }
#undef X25519_KEY_SIZE
#endif

    if ((evppeer = EVP_PKEY_new()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }
    if (EVP_PKEY_copy_parameters(evppeer, ctx->privkey) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_set1_tls_encodedpoint(evppeer, peerkey.data(), peerkey.length()) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if ((evpctx = EVP_PKEY_CTX_new(ctx->privkey, NULL)) == NULL) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_derive_init(evpctx) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_derive_set_peer(evpctx, evppeer) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_derive(evpctx, NULL, &length) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    secret->length(length);
    if (EVP_PKEY_derive(evpctx, secret->data(), &length) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }

    if (secret->length() > length) {
        ZuClear(secret->data() + length, secret->length() - length);
        secret->length(length);
    }
    ret = 0;
Exit:
    if (evpctx != NULL)
        EVP_PKEY_CTX_free(evpctx);
    if (evppeer != NULL)
        EVP_PKEY_free(evppeer);
    if (ret != 0 && secret)
        secret->null();
    if (release) {
        evp_keyex_free(ctx);
        *_ctx = NULL;
    }
    return ret;
}

/**
 * Upon success, ownership of `pkey` is transferred to the object being created. Otherwise, the refcount remains unchanged.
 */
static int evp_keyex_init(key_exchange_algorithm_t *algo, key_exchange_context_t **_ctx, EVP_PKEY *pkey)
{
    struct st_evp_keyex_context_t *ctx = NULL;
    uint8_t *key_bytes = nullptr;
    size_t length;
    int ret;

    ctx = new (EvpKeyExchangeHeap::alloc(sizeof(st_evp_keyex_context_t)))
        st_evp_keyex_context_t{{algo, {}, evp_keyex_on_exchange}, pkey};

    /* set public key */
    if (!(length = EVP_PKEY_get1_tls_encodedpoint(ctx->privkey, &key_bytes))) {
        OPENSSL_free(key_bytes);
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }

    ctx->super.pubkey = {key_bytes, length};
    *_ctx = &ctx->super;
    ret = 0;
Exit:
    if (ret != 0 && ctx != NULL) {
        ctx->privkey = NULL; /* do not decrement refcount of pkey in case of error */
        evp_keyex_free(ctx);
    }
    return ret;
}

static int evp_keyex_create(key_exchange_algorithm_t *algo, key_exchange_context_t **ctx)
{
    EVP_PKEY_CTX *evpctx = NULL;
    EVP_PKEY *pkey = NULL;
    int ret;

    /* generate private key */
    if ((evpctx = EVP_PKEY_CTX_new_id((int)algo->data, NULL)) == NULL) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_keygen_init(evpctx) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_keygen(evpctx, &pkey) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }

    /* setup */
    if ((ret = evp_keyex_init(algo, ctx, pkey)) != 0)
        goto Exit;
    pkey = NULL;
    ret = 0;

Exit:
    if (pkey != NULL)
        EVP_PKEY_free(pkey);
    if (evpctx != NULL)
        EVP_PKEY_CTX_free(evpctx);
    return ret;
}

static int evp_keyex_exchange(key_exchange_algorithm_t *algo, KeyExchangeBytes *outpubkey, KeyExchangeBytes *secret,
                              Bytes peerkey)
{
    key_exchange_context_t *ctx = NULL;
    int ret;

    outpubkey->null();

    if ((ret = evp_keyex_create(algo, &ctx)) != 0)
        goto Exit;
    outpubkey->copy(ctx->pubkey.data(), ctx->pubkey.length());
    ret = evp_keyex_on_exchange(&ctx, 1, secret, peerkey);
    assert(ctx == NULL);

Exit:
    if (ctx != NULL)
        evp_keyex_on_exchange(&ctx, 1, NULL, wire_bytes(NULL, 0));
    if (ret != 0)
        outpubkey->null();
    return ret;
}

#endif

#if defined(OPENSSL_IS_BORINGSSL) && Ztls_OPENSSL_HAVE_X25519MLKEM768

struct st_x25519mlkem768_context_t {
    key_exchange_context_t super;
    uint8_t pubkey[MLKEM768_PUBLIC_KEY_BYTES + X25519_PUBLIC_VALUE_LEN];
    struct {
        uint8_t x25519[X25519_PRIVATE_KEY_LEN];
        struct MLKEM768_private_key mlkem;
    } privkey;
};
using HybridExchangeHeap = ZmVHeap<"Ztls.Engine.HybridKeyExchange", 64, 8192, alignof(st_x25519mlkem768_context_t)>;

static int x25519mlkem768_on_exchange(key_exchange_context_t **_ctx, int release, KeyExchangeBytes *secret,
                                      Bytes ciphertext)
{
    struct st_x25519mlkem768_context_t *ctx = (void *)*_ctx;
    int ret;

    if (secret == NULL) {
        ret = 0;
        goto Exit;
    }

    secret->null();

    /* validate length */
    if (ciphertext.length() != MLKEM768_CIPHERTEXT_BYTES + X25519_PUBLIC_VALUE_LEN) {
        ret = Ztls_ALERT_DECRYPT_ERROR;
        goto Exit;
    }

    /* appsocate memory */
    secret->length(MLKEM_SHARED_SECRET_BYTES + X25519_SHARED_KEY_LEN);

    /* run key exchange */
    if (!MLKEM768_decap(secret->data(), ciphertext.data(), MLKEM768_CIPHERTEXT_BYTES, &ctx->privkey.mlkem) ||
        !X25519(secret->data() + MLKEM_SHARED_SECRET_BYTES, ctx->privkey.x25519, ciphertext.data() + MLKEM768_CIPHERTEXT_BYTES)) {
        ret = Ztls_ALERT_ILLEGAL_PARAMETER;
        goto Exit;
    }
    ret = 0;

Exit:
    if (secret != NULL && ret != 0) {
        secret->null();
    }
    if (release) {
        ZuClear(&ctx->privkey, sizeof(ctx->privkey));
        HybridExchangeHeap::free(ctx, sizeof(*ctx));
        *_ctx = NULL;
    }
    return ret;
}

static int x25519mlkem768_create(key_exchange_algorithm_t *algo, key_exchange_context_t **_ctx)
{
    auto ctx = new (HybridExchangeHeap::alloc(sizeof(st_x25519mlkem768_context_t))) st_x25519mlkem768_context_t{};

    ctx->super = (key_exchange_context_t){algo, wire_bytes(ctx->pubkey, sizeof(ctx->pubkey)), x25519mlkem768_on_exchange};
    MLKEM768_generate_key(ctx->pubkey, NULL, &ctx->privkey.mlkem);
    X25519_keypair(ctx->pubkey + MLKEM768_PUBLIC_KEY_BYTES, ctx->privkey.x25519);

    *_ctx = &ctx->super;
    return 0;
}

static int x25519mlkem768_exchange(key_exchange_algorithm_t *algo, KeyExchangeBytes *ciphertext, KeyExchangeBytes *secret,
                                   Bytes peerkey)
{
    struct {
        CBS cbs;
        struct MLKEM768_public_key key;
    } mlkem_peer;
    uint8_t x25519_privkey[X25519_PRIVATE_KEY_LEN];
    int ret;

    ciphertext->null();
    secret->null();

    /* validate input length */
    if (peerkey.length() != MLKEM768_PUBLIC_KEY_BYTES + X25519_PUBLIC_VALUE_LEN) {
        ret = Ztls_ALERT_DECODE_ERROR;
        goto Exit;
    }

    /* allocate memory */
    ciphertext->length(MLKEM768_CIPHERTEXT_BYTES + X25519_PUBLIC_VALUE_LEN);
    secret->length(MLKEM_SHARED_SECRET_BYTES + X25519_SHARED_KEY_LEN);

    /* run key exchange */
    CBS_init(&mlkem_peer.cbs, peerkey.data(), MLKEM768_PUBLIC_KEY_BYTES);
    X25519_keypair(ciphertext->data() + MLKEM768_CIPHERTEXT_BYTES, x25519_privkey);
    if (!MLKEM768_parse_public_key(&mlkem_peer.key, &mlkem_peer.cbs) ||
        !X25519(secret->data() + MLKEM_SHARED_SECRET_BYTES, x25519_privkey, peerkey.data() + MLKEM768_PUBLIC_KEY_BYTES)) {
        ret = Ztls_ALERT_ILLEGAL_PARAMETER;
        goto Exit;
    }
    MLKEM768_encap(ciphertext->data(), secret->data(), &mlkem_peer.key);

    ret = 0;

Exit:
    if (ret != 0) {
        ciphertext->null();
        secret->null();
    }
    ZuClear(&x25519_privkey, sizeof(x25519_privkey));
    return ret;
}

#endif

#if Ztls_OPENSSL_HAVE_MLKEM

static int evp_kem_on_exchange(key_exchange_context_t **_ctx, int release, KeyExchangeBytes *secret, Bytes ciphertext)
{
    struct st_evp_keyex_context_t *ctx = reinterpret_cast<struct st_evp_keyex_context_t *>(*_ctx);
    EVP_PKEY_CTX *evpctx = NULL;
    size_t length = 0;
    int ret;

    if (secret == NULL) {
        ret = 0;
        goto Exit;
    }

    secret->null();

    if ((evpctx = EVP_PKEY_CTX_new_from_pkey(NULL, ctx->privkey, NULL)) == NULL) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_decapsulate_init(evpctx, NULL) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_decapsulate(evpctx, NULL, &length, ciphertext.data(), ciphertext.length()) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    secret->length(length);
    if (EVP_PKEY_decapsulate(evpctx, secret->data(), &length, ciphertext.data(), ciphertext.length()) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (secret->length() > length)
        ZuClear(secret->data() + length, secret->length() - length);
    secret->length(length);
    ret = 0;

Exit:
    if (evpctx != NULL)
        EVP_PKEY_CTX_free(evpctx);
    if (ret != 0 && secret)
        secret->null();
    if (release) {
        evp_keyex_free(ctx);
        *_ctx = NULL;
    }
    return ret;
}

static int evp_kem_create(key_exchange_algorithm_t *algo, key_exchange_context_t **_ctx)
{
    struct st_evp_keyex_context_t *ctx = NULL;
    EVP_PKEY_CTX *evpctx = NULL;
    uint8_t *key_bytes = nullptr;
    size_t length;
    int ret;

    ctx = new (EvpKeyExchangeHeap::alloc(sizeof(st_evp_keyex_context_t)))
        st_evp_keyex_context_t{{algo, {}, evp_kem_on_exchange}, NULL};

    /* generate private key */
    if ((evpctx = EVP_PKEY_CTX_new_from_name(NULL, (const char *)algo->data, NULL)) == NULL) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_keygen_init(evpctx) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_generate(evpctx, &ctx->privkey) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }

    /* set public key */
    if (!(length = EVP_PKEY_get1_encoded_public_key(ctx->privkey, &key_bytes))) {
        OPENSSL_free(key_bytes);
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }

    ctx->super.pubkey = {key_bytes, length};
    *_ctx = &ctx->super;
    ret = 0;

Exit:
    if (ret != 0 && ctx != NULL)
        evp_keyex_free(ctx);
    if (evpctx != NULL)
        EVP_PKEY_CTX_free(evpctx);
    return ret;
}

static int evp_kem_exchange(key_exchange_algorithm_t *algo, KeyExchangeBytes *ciphertext, KeyExchangeBytes *secret,
                            Bytes peerkey)
{
    EVP_PKEY_CTX *evpctx = NULL;
    EVP_PKEY *key = NULL;
    size_t ciphertext_length = 0, secret_length = 0;
    int ret;

    ciphertext->null();
    secret->null();

    if ((evpctx = EVP_PKEY_CTX_new_from_name(NULL, (const char *)algo->data, NULL)) == NULL) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_paramgen_init(evpctx) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_paramgen(evpctx, &key) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_set1_encoded_public_key(key, peerkey.data(), peerkey.length()) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }

    EVP_PKEY_CTX_free(evpctx);
    evpctx = EVP_PKEY_CTX_new_from_pkey(NULL, key, NULL);
    if (evpctx == NULL) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_encapsulate_init(evpctx, NULL) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (EVP_PKEY_encapsulate(evpctx, NULL, &ciphertext_length, NULL, &secret_length) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    ciphertext->length(ciphertext_length);
    secret->length(secret_length);
    if (EVP_PKEY_encapsulate(evpctx, ciphertext->data(), &ciphertext_length, secret->data(), &secret_length) <= 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (secret->length() > secret_length)
        ZuClear(secret->data() + secret_length, secret->length() - secret_length);
    secret->length(secret_length);
    ciphertext->length(ciphertext_length);
    ret = 0;

Exit:
    if (evpctx != NULL)
        EVP_PKEY_CTX_free(evpctx);
    if (key != NULL)
        EVP_PKEY_free(key);
    if (ret != 0) {
        secret->null();
        ciphertext->null();
    }
    return ret;
}

#endif

int openssl_create_key_exchange(key_exchange_context_t **ctx, EVP_PKEY *pkey)
{
    int ret, id;

    switch (id = EVP_PKEY_id(pkey)) {

    case EVP_PKEY_EC: {
        /* obtain eckey */
        EC_KEY *eckey = EVP_PKEY_get1_EC_KEY(pkey);

        /* determine algo */
        key_exchange_algorithm_t *algo;
        switch (EC_GROUP_get_curve_name(EC_KEY_get0_group(eckey))) {
        case NID_X9_62_prime256v1:
            algo = &openssl_secp256r1;
            break;
#if Ztls_OPENSSL_HAVE_SECP384R1
        case NID_secp384r1:
            algo = &openssl_secp384r1;
            break;
#endif
#if Ztls_OPENSSL_HAVE_SECP521R1
        case NID_secp521r1:
            algo = &openssl_secp521r1;
            break;
#endif
        default:
            EC_KEY_free(eckey);
            return Ztls_ERROR_INCOMPATIBLE_KEY;
        }

        /* load key */
        if ((ret = x9_62_init_key(algo, ctx, eckey)) != 0) {
            EC_KEY_free(eckey);
            return ret;
        }

        return 0;
    } break;

#if Ztls_OPENSSL_HAVE_X25519
    case NID_X25519:
        if ((ret = evp_keyex_init(&openssl_x25519, ctx, pkey)) != 0)
            return ret;
        EVP_PKEY_up_ref(pkey);
        return 0;
#endif

    default:
        return Ztls_ERROR_INCOMPATIBLE_KEY;
    }
}

#if Ztls_OPENSSL_HAVE_ASYNC

static void retire_sign_digest(Ztls::Cxn *tls)
{
    // A paused native operation is retired by the original Rx owner after
    // readiness. Give the provider a chance to finish consuming its result.
    if (tls->signature_job) {
        int ret;
        while (ASYNC_start_job(&tls->signature_job, tls->signature_wait, &ret, nullptr, nullptr, 0) == ASYNC_PAUSE)
            ;
    }
    EVP_MD_CTX_destroy(tls->signature_digest);
    ASYNC_WAIT_CTX_free(tls->signature_wait);
    tls->signature_digest = nullptr;
    tls->signature_wait = nullptr;
    tls->signature_job = nullptr;
    tls->signature_length = 0;
    tls->signature_algorithm = 0;
    tls->signature.null();
}

static int sign_digest_job(void *arg)
{
    auto tls = *static_cast<Ztls::Cxn **>(arg);
    return EVP_DigestSignFinal(tls->signature_digest, tls->signature.data(), &tls->signature_length);
}

static int resume_sign_digest(Ztls::Cxn *tls, buffer_t *outbuf, uint16_t *selected)
{
    *selected = tls->signature_algorithm;
    int ret;
    switch (ASYNC_start_job(&tls->signature_job, tls->signature_wait, &ret, sign_digest_job, &tls, sizeof(tls))) {
    case ASYNC_PAUSE: {
#ifndef _WIN32
        size_t count = 0;
        ASYNC_WAIT_CTX_get_all_fds(tls->signature_wait, nullptr, &count);
        assert(count == 1);
        OSSL_ASYNC_FD fd;
        if (count == 1 && ASYNC_WAIT_CTX_get_all_fds(tls->signature_wait, &fd, &count))
            tls->signature_fd = fd;
#endif
        return Ztls_ERROR_ASYNC_OPERATION;
    }
    case ASYNC_FINISH:
        tls->signature_job = nullptr;
        if (ret != 1) {
            ret = Ztls_ERROR_LIBRARY;
            break;
        }
        Ztls_buffer_pushv(outbuf, tls->signature.data(), tls->signature_length);
        ret = 0;
        break;
    default:
        ret = Ztls_ERROR_LIBRARY;
        break;
    }

Exit:
    tls->retireSignature();
    return ret;
}

static int start_sign_digest(Ztls::Cxn *tls, EVP_MD_CTX *digest, uint16_t algorithm,
                             size_t length, buffer_t *outbuf)
{
    tls->signature_digest = digest;
    tls->signature_wait = ASYNC_WAIT_CTX_new();
    if (!tls->signature_wait) {
        retire_sign_digest(tls);
        return Ztls_ERROR_NO_MEMORY;
    }
    tls->signature_algorithm = algorithm;
    tls->signature_length = length;
    tls->signature.length(length);
    memset(tls->signature.data(), 0, length);
    tls->signature_retire = [tls]() { retire_sign_digest(tls); };
    tls->signature_resume = [tls](buffer_t *output, uint16_t *selected) {
        return resume_sign_digest(tls, output, selected);
    };
    return resume_sign_digest(tls, outbuf, &algorithm);
}

#endif

static int do_sign(EVP_PKEY *key, const openssl_signature_scheme_t *scheme, buffer_t *outbuf, Bytes input,
                   Ztls::Cxn *async)
{
    EVP_MD_CTX *ctx = NULL;
    const EVP_MD *md = scheme->scheme_md != NULL ? scheme->scheme_md() : NULL;
    EVP_PKEY_CTX *pkey_ctx;
    size_t siglen;
    int ret;

    if ((ctx = EVP_MD_CTX_create()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }

    if (EVP_DigestSignInit(ctx, &pkey_ctx, md, NULL, key) != 1) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }

#if Ztls_OPENSSL_HAVE_ED25519
    if (EVP_PKEY_id(key) == EVP_PKEY_ED25519) {
        /* ED25519 requires the use of the all-at-once function that appeared in OpenSSL 1.1.1, hence different path */
        if (EVP_DigestSign(ctx, NULL, &siglen, input.data(), input.length()) != 1) {
            ret = Ztls_ERROR_LIBRARY;
            goto Exit;
        }
        if ((ret = buffer_reserve(outbuf, siglen, outbuf->tx)) != 0)
            goto Exit;
        if (EVP_DigestSign(ctx, outbuf->base + outbuf->off, &siglen, input.data(), input.length()) != 1) {
            ret = Ztls_ERROR_LIBRARY;
            goto Exit;
        }
    } else
#endif
    {
        if (EVP_PKEY_id(key) == EVP_PKEY_RSA) {
            if (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) != 1) {
                ret = Ztls_ERROR_LIBRARY;
                goto Exit;
            }
            if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, -1) != 1) {
                ret = Ztls_ERROR_LIBRARY;
                goto Exit;
            }
            if (EVP_PKEY_CTX_set_rsa_mgf1_md(pkey_ctx, md) != 1) {
                ret = Ztls_ERROR_LIBRARY;
                goto Exit;
            }
        }
        if (EVP_DigestSignUpdate(ctx, input.data(), input.length()) != 1) {
            ret = Ztls_ERROR_LIBRARY;
            goto Exit;
        }
        if (EVP_DigestSignFinal(ctx, NULL, &siglen) != 1) {
            ret = Ztls_ERROR_LIBRARY;
            goto Exit;
        }
        /* If permitted by the caller (by providing a non-NULL `async` slot), use the asynchronous signing method and return
         * immediately. */
#if Ztls_OPENSSL_HAVE_ASYNC
        if (async)
            return start_sign_digest(async, ctx, scheme->scheme_id, siglen, outbuf);
#endif
        /* Otherwise, generate signature synchronously. */
        if ((ret = buffer_reserve(outbuf, siglen, outbuf->tx)) != 0)
            goto Exit;
        if (EVP_DigestSignFinal(ctx, outbuf->base + outbuf->off, &siglen) != 1) {
            ret = Ztls_ERROR_LIBRARY;
            goto Exit;
        }
    }

    outbuf->off += siglen;

    ret = 0;
Exit:
    if (ctx != NULL)
        EVP_MD_CTX_destroy(ctx);
    return ret;
}

struct openssl_cipher_context_t {
    cipher_context_t super;
    EVP_CIPHER_CTX *evp;
};

static void cipher_dispose(cipher_context_t *_ctx)
{
    struct openssl_cipher_context_t *ctx = (struct openssl_cipher_context_t *)_ctx;
    EVP_CIPHER_CTX_free(ctx->evp);
}

static void cipher_do_init(cipher_context_t *_ctx, const void *iv)
{
    struct openssl_cipher_context_t *ctx = (struct openssl_cipher_context_t *)_ctx;
    int ret;
    ret = EVP_EncryptInit_ex(ctx->evp, NULL, NULL, NULL, static_cast<const unsigned char *>(iv));
    assert(ret);
}

static int cipher_setup_crypto(cipher_context_t *_ctx, int is_enc, const void *key, const EVP_CIPHER *cipher,
                               void (*do_transform)(cipher_context_t *, void *, const void *, size_t))
{
    struct openssl_cipher_context_t *ctx = (struct openssl_cipher_context_t *)_ctx;

    ctx->super.do_dispose = cipher_dispose;
    ctx->super.do_init = cipher_do_init;
    ctx->super.do_transform = do_transform;

    if ((ctx->evp = EVP_CIPHER_CTX_new()) == NULL)
        return Ztls_ERROR_NO_MEMORY;

    if (is_enc) {
        if (!EVP_EncryptInit_ex(ctx->evp, cipher, NULL, static_cast<const unsigned char *>(key), NULL))
            goto Error;
    } else {
        if (!EVP_DecryptInit_ex(ctx->evp, cipher, NULL, static_cast<const unsigned char *>(key), NULL))
            goto Error;
        EVP_CIPHER_CTX_set_padding(ctx->evp, 0); /* required to disable one block buffering in ECB mode */
    }

    return 0;
Error:
    EVP_CIPHER_CTX_free(ctx->evp);
    return Ztls_ERROR_LIBRARY;
}

static void openssl_cipher_encrypt(cipher_context_t *_ctx, void *output, const void *input, size_t _len)
{
    struct openssl_cipher_context_t *ctx = (struct openssl_cipher_context_t *)_ctx;
    int len = (int)_len, ret = EVP_EncryptUpdate(ctx->evp, static_cast<unsigned char *>(output), &len, static_cast<const unsigned char *>(input), len);
    assert(ret);
    assert(len == (int)_len);
}

static void cipher_decrypt(cipher_context_t *_ctx, void *output, const void *input, size_t _len)
{
    struct openssl_cipher_context_t *ctx = (struct openssl_cipher_context_t *)_ctx;
    int len = (int)_len, ret = EVP_DecryptUpdate(ctx->evp, static_cast<unsigned char *>(output), &len, static_cast<const unsigned char *>(input), len);
    assert(ret);
    assert(len == (int)_len);
}

static int aes128ecb_setup_crypto(cipher_context_t *ctx, int is_enc, const void *key)
{
    return cipher_setup_crypto(ctx, is_enc, key, EVP_aes_128_ecb(), is_enc ? openssl_cipher_encrypt : cipher_decrypt);
}

static int aes256ecb_setup_crypto(cipher_context_t *ctx, int is_enc, const void *key)
{
    return cipher_setup_crypto(ctx, is_enc, key, EVP_aes_256_ecb(), is_enc ? openssl_cipher_encrypt : cipher_decrypt);
}

static int aes128ctr_setup_crypto(cipher_context_t *ctx, int is_enc, const void *key)
{
    return cipher_setup_crypto(ctx, 1, key, EVP_aes_128_ctr(), openssl_cipher_encrypt);
}

static int aes256ctr_setup_crypto(cipher_context_t *ctx, int is_enc, const void *key)
{
    return cipher_setup_crypto(ctx, 1, key, EVP_aes_256_ctr(), openssl_cipher_encrypt);
}

#if Ztls_OPENSSL_HAVE_CHACHA20_POLY1305
#ifdef OPENSSL_IS_BORINGSSL

struct boringssl_chacha20_context_t {
    cipher_context_t super;
    uint8_t key[Ztls_CHACHA20_KEY_SIZE];
    uint8_t iv[12];
    struct {
        uint32_t ctr;
        uint8_t bytes[64];
        size_t len;
    } keystream;
};

static void boringssl_chacha20_dispose(cipher_context_t *_ctx)
{
    struct boringssl_chacha20_context_t *ctx = (struct boringssl_chacha20_context_t *)_ctx;

    ZuClear(ctx->key, sizeof(ctx->key));
    ZuClear(ctx->iv, sizeof(ctx->iv));
    ZuClear(ctx->keystream.bytes, sizeof(ctx->keystream.bytes));
}

static void boringssl_chacha20_init(cipher_context_t *_ctx, const void *_iv)
{
    struct boringssl_chacha20_context_t *ctx = (struct boringssl_chacha20_context_t *)_ctx;
    const uint8_t *iv = _iv;

    memcpy(ctx->iv, iv + 4, sizeof(ctx->iv));
    ctx->keystream.ctr = iv[0] | ((uint32_t)iv[1] << 8) | ((uint32_t)iv[2] << 16) | ((uint32_t)iv[3] << 24);
    ctx->keystream.len = 0;
}

static inline void boringssl_chacha20_transform_buffered(struct boringssl_chacha20_context_t *ctx, uint8_t **output,
                                                         const uint8_t **input, size_t *len)
{
    size_t apply_len = *len < ctx->keystream.len ? *len : ctx->keystream.len;
    const uint8_t *ks = ctx->keystream.bytes + sizeof(ctx->keystream.bytes) - ctx->keystream.len;
    ctx->keystream.len -= apply_len;

    *len -= apply_len;
    for (size_t i = 0; i < apply_len; ++i)
        *(*output)++ = *(*input)++ ^ *ks++;
}

static void boringssl_chacha20_transform(cipher_context_t *_ctx, void *_output, const void *_input, size_t len)
{
    struct boringssl_chacha20_context_t *ctx = (struct boringssl_chacha20_context_t *)_ctx;
    uint8_t *output = _output;
    const uint8_t *input = _input;

    if (len == 0)
        return;

    if (ctx->keystream.len != 0) {
        boringssl_chacha20_transform_buffered(ctx, &output, &input, &len);
        if (len == 0)
            return;
    }

    assert(ctx->keystream.len == 0);

    if (len >= sizeof(ctx->keystream.bytes)) {
        size_t blocks = len / CHACHA20POLY1305_BLOCKSIZE;
        CRYPTO_chacha_20(output, input, blocks * CHACHA20POLY1305_BLOCKSIZE, ctx->key, ctx->iv, ctx->keystream.ctr);
        ctx->keystream.ctr += blocks;
        output += blocks * CHACHA20POLY1305_BLOCKSIZE;
        input += blocks * CHACHA20POLY1305_BLOCKSIZE;
        len -= blocks * CHACHA20POLY1305_BLOCKSIZE;
        if (len == 0)
            return;
    }

    memset(ctx->keystream.bytes, 0, CHACHA20POLY1305_BLOCKSIZE);
    CRYPTO_chacha_20(ctx->keystream.bytes, ctx->keystream.bytes, CHACHA20POLY1305_BLOCKSIZE, ctx->key, ctx->iv,
                     ctx->keystream.ctr++);
    ctx->keystream.len = sizeof(ctx->keystream.bytes);

    boringssl_chacha20_transform_buffered(ctx, &output, &input, &len);
    assert(len == 0);
}

static int boringssl_chacha20_setup_crypto(cipher_context_t *_ctx, int is_enc, const void *key)
{
    struct boringssl_chacha20_context_t *ctx = (struct boringssl_chacha20_context_t *)_ctx;

    ctx->super.do_dispose = boringssl_chacha20_dispose;
    ctx->super.do_init = boringssl_chacha20_init;
    ctx->super.do_transform = boringssl_chacha20_transform;
    memcpy(ctx->key, key, sizeof(ctx->key));

    return 0;
}

#else

static int chacha20_setup_crypto(cipher_context_t *ctx, int is_enc, const void *key)
{
    return cipher_setup_crypto(ctx, 1, key, EVP_chacha20(), openssl_cipher_encrypt);
}

#endif
#endif

#if Ztls_OPENSSL_HAVE_BF

static int bfecb_setup_crypto(cipher_context_t *ctx, int is_enc, const void *key)
{
    return cipher_setup_crypto(ctx, is_enc, key, EVP_bf_ecb(), is_enc ? openssl_cipher_encrypt : cipher_decrypt);
}

#endif

struct aead_crypto_context_t {
    aead_context_t super;
    EVP_CIPHER_CTX *evp_ctx;
    uint8_t static_iv[Ztls_MAX_IV_SIZE];
    int dec_active;
};

static void aead_dispose_crypto(aead_context_t *_ctx)
{
    struct aead_crypto_context_t *ctx = (struct aead_crypto_context_t *)_ctx;

    if (ctx->evp_ctx != NULL)
        EVP_CIPHER_CTX_free(ctx->evp_ctx);
}

static void openssl_aead_get_iv(aead_context_t *_ctx, void *iv)
{
    struct aead_crypto_context_t *ctx = (struct aead_crypto_context_t *)_ctx;

    memcpy(iv, ctx->static_iv, ctx->super.algo->iv_size);
}

static void openssl_aead_set_iv(aead_context_t *_ctx, const void *iv)
{
    struct aead_crypto_context_t *ctx = (struct aead_crypto_context_t *)_ctx;

    memcpy(ctx->static_iv, iv, ctx->super.algo->iv_size);
}

static int aead_enc_begin(aead_context_t *_ctx, uint64_t seq, const void *aad, size_t aadlen)
{
    struct aead_crypto_context_t *ctx = reinterpret_cast<struct aead_crypto_context_t *>(_ctx);
    uint8_t iv[Ztls_MAX_IV_SIZE];
    int len;
    if (aadlen > INT_MAX)
        return Ztls_ERROR_LIBRARY;
    aead__build_iv(_ctx->algo, iv, ctx->static_iv, seq);
    if (!EVP_EncryptInit_ex(ctx->evp_ctx, NULL, NULL, NULL, iv) ||
        (aadlen != 0 && !EVP_EncryptUpdate(ctx->evp_ctx, NULL, &len, static_cast<const unsigned char *>(aad), (int)aadlen)))
        return Ztls_ERROR_LIBRARY;
    return 0;
}

static int aead_enc_chunk(aead_context_t *_ctx, void *output, const void *input, size_t length, size_t *written)
{
    struct aead_crypto_context_t *ctx = reinterpret_cast<struct aead_crypto_context_t *>(_ctx);
    int len;
    *written = 0;
    if (length > INT_MAX || !EVP_EncryptUpdate(ctx->evp_ctx, static_cast<unsigned char *>(output), &len, static_cast<const unsigned char *>(input), (int)length) || len != length)
        return Ztls_ERROR_LIBRARY;
    *written = len;
    return 0;
}

static int aead_enc_end(aead_context_t *_ctx, void *output, size_t capacity, size_t *written)
{
    struct aead_crypto_context_t *ctx = reinterpret_cast<struct aead_crypto_context_t *>(_ctx);
    uint8_t final[EVP_MAX_BLOCK_LENGTH];
    int len;
    *written = 0;
    if (capacity < _ctx->algo->tag_size || !EVP_EncryptFinal_ex(ctx->evp_ctx, final, &len) || len != 0 ||
        !EVP_CIPHER_CTX_ctrl(ctx->evp_ctx, EVP_CTRL_GCM_GET_TAG, (int)_ctx->algo->tag_size, output))
        return Ztls_ERROR_LIBRARY;
    *written = _ctx->algo->tag_size;
    return 0;
}

/* Only GCM and ChaCha20-Poly1305 install these callbacks. Their update calls
 * emit exactly the input size; final emits no additional plaintext. */
static int aead_dec_error(struct aead_crypto_context_t *ctx, int error)
{
    ctx->dec_active = 0;
    EVP_CIPHER_CTX_reset(ctx->evp_ctx);
    return error;
}

static void aead_dec_abort(aead_context_t *_ctx)
{
    aead_dec_error(reinterpret_cast<struct aead_crypto_context_t *>((void *)_ctx), Ztls_ERROR_LIBRARY);
}

static int aead_dec_begin(aead_context_t *_ctx, uint64_t seq, const void *aad, size_t aadlen)
{
    struct aead_crypto_context_t *ctx = reinterpret_cast<struct aead_crypto_context_t *>(_ctx);
    uint8_t iv[Ztls_MAX_IV_SIZE];
    int len;
    if (ctx->dec_active || aadlen > INT_MAX || (aadlen != 0 && aad == NULL))
        return aead_dec_error(ctx, Ztls_ERROR_LIBRARY);
    aead__build_iv(_ctx->algo, iv, ctx->static_iv, seq);
    int ret = EVP_DecryptInit_ex(ctx->evp_ctx, NULL, NULL, NULL, iv);
    ZuClear(iv, sizeof(iv));
    if (!ret || (aadlen != 0 && !EVP_DecryptUpdate(ctx->evp_ctx, NULL, &len, static_cast<const unsigned char *>(aad), (int)aadlen)))
        return aead_dec_error(ctx, Ztls_ERROR_LIBRARY);
    ctx->dec_active = 1;
    return 0;
}

static int aead_dec_chunk(aead_context_t *_ctx, void *output, size_t capacity,
                          const void *input, size_t length, size_t *written)
{
    struct aead_crypto_context_t *ctx = reinterpret_cast<struct aead_crypto_context_t *>(_ctx);
    int len;
    *written = 0;
    if (!ctx->dec_active || length > capacity || length > INT_MAX ||
        (length != 0 && (output == NULL || input == NULL)))
        return aead_dec_error(ctx, Ztls_ERROR_LIBRARY);
    if (length != 0 && (!EVP_DecryptUpdate(ctx->evp_ctx, static_cast<unsigned char *>(output), &len, static_cast<const unsigned char *>(input), (int)length) || len != length))
        return aead_dec_error(ctx, Ztls_ERROR_LIBRARY);
    *written = length;
    return 0;
}

static int aead_dec_end(aead_context_t *_ctx, const void *tag, size_t taglen)
{
    struct aead_crypto_context_t *ctx = reinterpret_cast<struct aead_crypto_context_t *>(_ctx);
    uint8_t final[EVP_MAX_BLOCK_LENGTH];
    int len;
    if (!ctx->dec_active || tag == NULL || taglen != _ctx->algo->tag_size ||
        !EVP_CIPHER_CTX_ctrl(ctx->evp_ctx, EVP_CTRL_GCM_SET_TAG, (int)taglen, (void *)tag))
        return aead_dec_error(ctx, Ztls_ERROR_LIBRARY);
    int ret = EVP_DecryptFinal_ex(ctx->evp_ctx, final, &len);
    ZuClear(final, sizeof(final));
    if (!ret) {
        /* Rejected early data may be skipped before the next protected record.
         * Reset the per-record provider state while retaining the installed key.
         * A fresh nonce on begin replaces this unused clearing nonce. */
        uint8_t cleared_iv[Ztls_MAX_IV_SIZE] = {0};
        ctx->dec_active = 0;
        if (!EVP_DecryptInit_ex(ctx->evp_ctx, NULL, NULL, NULL, cleared_iv))
            return aead_dec_error(ctx, Ztls_ERROR_LIBRARY);
        return Ztls_ALERT_BAD_RECORD_MAC;
    }
    if (len != 0)
        return aead_dec_error(ctx, Ztls_ERROR_LIBRARY);
    ctx->dec_active = 0;
    return 0;
}

static void aead_do_encrypt_init(aead_context_t *_ctx, uint64_t seq, const void *aad, size_t aadlen)
{
    struct aead_crypto_context_t *ctx = (struct aead_crypto_context_t *)_ctx;
    uint8_t iv[Ztls_MAX_IV_SIZE];
    int ret;

    aead__build_iv(ctx->super.algo, iv, ctx->static_iv, seq);
    ret = EVP_EncryptInit_ex(ctx->evp_ctx, NULL, NULL, NULL, iv);
    assert(ret);

    if (aadlen != 0) {
        int blocklen;
        ret = EVP_EncryptUpdate(ctx->evp_ctx, NULL, &blocklen, static_cast<const unsigned char *>(aad), (int)aadlen);
        assert(ret);
    }
}

static size_t aead_do_encrypt_update(aead_context_t *_ctx, void *output, const void *input, size_t inlen)
{
    struct aead_crypto_context_t *ctx = (struct aead_crypto_context_t *)_ctx;
    int blocklen, ret;

    ret = EVP_EncryptUpdate(ctx->evp_ctx, static_cast<unsigned char *>(output), &blocklen, static_cast<const unsigned char *>(input), (int)inlen);
    assert(ret);

    return blocklen;
}

static size_t aead_do_encrypt_final(aead_context_t *_ctx, void *_output)
{
    struct aead_crypto_context_t *ctx = (struct aead_crypto_context_t *)_ctx;
    uint8_t *output = static_cast<uint8_t *>(_output);
    size_t off = 0, tag_size = ctx->super.algo->tag_size;
    int blocklen, ret;

    ret = EVP_EncryptFinal_ex(ctx->evp_ctx, output + off, &blocklen);
    assert(ret);
    off += blocklen;
    ret = EVP_CIPHER_CTX_ctrl(ctx->evp_ctx, EVP_CTRL_GCM_GET_TAG, (int)tag_size, output + off);
    assert(ret);
    off += tag_size;

    return off;
}

static size_t aead_do_decrypt(aead_context_t *_ctx, void *_output, const void *input, size_t inlen, uint64_t seq,
                              const void *aad, size_t aadlen)
{
    struct aead_crypto_context_t *ctx = (struct aead_crypto_context_t *)_ctx;
    uint8_t *output = static_cast<uint8_t *>(_output), iv[Ztls_MAX_IV_SIZE];
    size_t off = 0, tag_size = ctx->super.algo->tag_size;
    int blocklen, ret;

    if (inlen < tag_size)
        return SIZE_MAX;

    aead__build_iv(ctx->super.algo, iv, ctx->static_iv, seq);
    ret = EVP_DecryptInit_ex(ctx->evp_ctx, NULL, NULL, NULL, iv);
    assert(ret);
    if (aadlen != 0) {
        ret = EVP_DecryptUpdate(ctx->evp_ctx, NULL, &blocklen, static_cast<const unsigned char *>(aad), (int)aadlen);
        assert(ret);
    }
    ret = EVP_DecryptUpdate(ctx->evp_ctx, output + off, &blocklen, static_cast<const unsigned char *>(input), (int)(inlen - tag_size));
    assert(ret);
    off += blocklen;
    if (!EVP_CIPHER_CTX_ctrl(ctx->evp_ctx, EVP_CTRL_GCM_SET_TAG, (int)tag_size, (void *)((uint8_t *)input + inlen - tag_size)))
        return SIZE_MAX;
    if (!EVP_DecryptFinal_ex(ctx->evp_ctx, output + off, &blocklen))
        return SIZE_MAX;
    off += blocklen;

    return off;
}

static int aead_setup_crypto(aead_context_t *_ctx, int is_enc, const void *key, const void *iv, const EVP_CIPHER *cipher)
{
    struct aead_crypto_context_t *ctx = (struct aead_crypto_context_t *)_ctx;
    int ret;

    ctx->dec_active = 0;
    ctx->super.do_dec_begin = is_enc ? NULL : aead_dec_begin;
    ctx->super.do_dec_chunk = is_enc ? NULL : aead_dec_chunk;
    ctx->super.do_dec_end = is_enc ? NULL : aead_dec_end;
    ctx->super.do_dec_abort = is_enc ? NULL : aead_dec_abort;
    ctx->super.dispose_crypto = aead_dispose_crypto;
    ctx->super.do_get_iv = openssl_aead_get_iv;
    ctx->super.do_set_iv = openssl_aead_set_iv;
    if (is_enc) {
        ctx->super.do_enc_begin = aead_enc_begin;
        ctx->super.do_enc_chunk = aead_enc_chunk;
        ctx->super.do_enc_end = aead_enc_end;
        ctx->super.do_encrypt_init = aead_do_encrypt_init;
        ctx->super.do_encrypt_update = aead_do_encrypt_update;
        ctx->super.do_encrypt_final = aead_do_encrypt_final;
        ctx->super.do_encrypt = aead__do_encrypt;
        ctx->super.do_encrypt_v = aead__do_encrypt_v;
        ctx->super.do_encrypt_v_s = aead__do_encrypt_v_s;
        ctx->super.do_decrypt = NULL;
    } else {
        ctx->super.do_encrypt_init = NULL;
        ctx->super.do_encrypt_update = NULL;
        ctx->super.do_encrypt_final = NULL;
        ctx->super.do_encrypt = NULL;
        ctx->super.do_encrypt_v = NULL;
        ctx->super.do_encrypt_v_s = NULL;
        ctx->super.do_decrypt = aead_do_decrypt;
    }
    ctx->evp_ctx = NULL;

    if ((ctx->evp_ctx = EVP_CIPHER_CTX_new()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Error;
    }
    if (is_enc) {
        if (!EVP_EncryptInit_ex(ctx->evp_ctx, cipher, NULL, static_cast<const unsigned char *>(key), NULL)) {
            ret = Ztls_ERROR_LIBRARY;
            goto Error;
        }
    } else {
        if (!EVP_DecryptInit_ex(ctx->evp_ctx, cipher, NULL, static_cast<const unsigned char *>(key), NULL)) {
            ret = Ztls_ERROR_LIBRARY;
            goto Error;
        }
    }
    if (!EVP_CIPHER_CTX_ctrl(ctx->evp_ctx, EVP_CTRL_GCM_SET_IVLEN, (int)ctx->super.algo->iv_size, NULL)) {
        ret = Ztls_ERROR_LIBRARY;
        goto Error;
    }

    memcpy(ctx->static_iv, iv, ctx->super.algo->iv_size);

    return 0;

Error:
    aead_dispose_crypto(&ctx->super);
    return ret;
}

static int aead_aes128gcm_setup_crypto(aead_context_t *ctx, int is_enc, const void *key, const void *iv)
{
    return aead_setup_crypto(ctx, is_enc, key, iv, EVP_aes_128_gcm());
}

static int aead_aes256gcm_setup_crypto(aead_context_t *ctx, int is_enc, const void *key, const void *iv)
{
    return aead_setup_crypto(ctx, is_enc, key, iv, EVP_aes_256_gcm());
}

#if Ztls_OPENSSL_HAVE_CHACHA20_POLY1305
#ifdef OPENSSL_IS_BORINGSSL

struct boringssl_chacha20poly1305_context_t {
    struct chacha20poly1305_context_t super;
    poly1305_state poly1305;
};

static void boringssl_poly1305_init(struct chacha20poly1305_context_t *_ctx, const void *key)
{
    struct boringssl_chacha20poly1305_context_t *ctx = (struct boringssl_chacha20poly1305_context_t *)_ctx;
    CRYPTO_poly1305_init(&ctx->poly1305, key);
}

static void boringssl_poly1305_update(struct chacha20poly1305_context_t *_ctx, const void *input, size_t len)
{
    struct boringssl_chacha20poly1305_context_t *ctx = (struct boringssl_chacha20poly1305_context_t *)_ctx;
    CRYPTO_poly1305_update(&ctx->poly1305, input, len);
}

static void boringssl_poly1305_finish(struct chacha20poly1305_context_t *_ctx, void *tag)
{
    struct boringssl_chacha20poly1305_context_t *ctx = (struct boringssl_chacha20poly1305_context_t *)_ctx;
    CRYPTO_poly1305_finish(&ctx->poly1305, tag);
}

static int boringssl_chacha20poly1305_setup_crypto(aead_context_t *ctx, int is_enc, const void *key, const void *iv)
{
    return chacha20poly1305_setup_crypto(ctx, is_enc, key, iv, &openssl_chacha20, boringssl_poly1305_init,
                                         boringssl_poly1305_update, boringssl_poly1305_finish);
}

#else

static int aead_chacha20poly1305_setup_crypto(aead_context_t *ctx, int is_enc, const void *key, const void *iv)
{
    return aead_setup_crypto(ctx, is_enc, key, iv, EVP_chacha20_poly1305());
}

#endif
#endif

#define _sha256_final(ctx, md) SHA256_Final(static_cast<uint8_t *>(md), (ctx))
Ztls_define_hash(sha256, SHA256_CTX, SHA256_Init, SHA256_Update, _sha256_final);

#define _sha384_final(ctx, md) SHA384_Final(static_cast<uint8_t *>(md), (ctx))
Ztls_define_hash(sha384, SHA512_CTX, SHA384_Init, SHA384_Update, _sha384_final);

#define _sha512_final(ctx, md) SHA512_Final(static_cast<uint8_t *>(md), (ctx))
Ztls_define_hash(sha512, SHA512_CTX, SHA512_Init, SHA512_Update, _sha512_final);

static int sign_certificate(sign_certificate_t *_self, Ztls::Cxn *tls, bool allow_async, uint16_t *selected_algorithm,
                            buffer_t *outbuf, Bytes input, const uint16_t *algorithms, size_t num_algorithms)
{
    auto self = reinterpret_cast<openssl_sign_certificate_t *>(_self);
    auto scheme = openssl_select_signature_scheme(self->schemes, algorithms, num_algorithms);
    if (!scheme)
        return Ztls_ALERT_HANDSHAKE_FAILURE;
    *selected_algorithm = scheme->scheme_id;

#if Ztls_OPENSSL_HAVE_ASYNC
    return do_sign(self->key, scheme, outbuf, input, allow_async && self->async ? tls : nullptr);
#else
    return do_sign(self->key, scheme, outbuf, input, nullptr);
#endif
}

static X509 *to_x509(Bytes vec)
{
    const uint8_t *p = vec.data();
    return d2i_X509(NULL, &p, (long)vec.length());
}

static int verify_sign(void *verify_ctx, uint16_t algo, Bytes data, Bytes signature)
{
    EVP_PKEY *key = static_cast<EVP_PKEY *>(verify_ctx);
    const openssl_signature_scheme_t *scheme;
    EVP_MD_CTX *ctx = NULL;
    EVP_PKEY_CTX *pkey_ctx = NULL;
    int ret = 0;

    if (data.data() == NULL)
        goto Exit;

    if ((scheme = openssl_lookup_signature_schemes(key)) == NULL) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    for (; scheme->scheme_id != UINT16_MAX; ++scheme)
        if (scheme->scheme_id == algo)
            goto SchemeFound;
    ret = Ztls_ALERT_ILLEGAL_PARAMETER;
    goto Exit;

SchemeFound:
    if ((ctx = EVP_MD_CTX_create()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }

#if Ztls_OPENSSL_HAVE_ED25519
    if (EVP_PKEY_id(key) == EVP_PKEY_ED25519) {
        /* ED25519 requires the use of the all-at-once function that appeared in OpenSSL 1.1.1, hence different path */
        if (EVP_DigestVerifyInit(ctx, &pkey_ctx, NULL, NULL, key) != 1) {
            ret = Ztls_ERROR_LIBRARY;
            goto Exit;
        }
        if (EVP_DigestVerify(ctx, signature.data(), signature.length(), data.data(), data.length()) != 1) {
            ret = Ztls_ERROR_LIBRARY;
            goto Exit;
        }
    } else
#endif
    {
        if (EVP_DigestVerifyInit(ctx, &pkey_ctx, scheme->scheme_md(), NULL, key) != 1) {
            ret = Ztls_ERROR_LIBRARY;
            goto Exit;
        }

        if (EVP_PKEY_id(key) == EVP_PKEY_RSA) {
            if (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) != 1) {
                ret = Ztls_ERROR_LIBRARY;
                goto Exit;
            }
            if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, -1) != 1) {
                ret = Ztls_ERROR_LIBRARY;
                goto Exit;
            }
            if (EVP_PKEY_CTX_set_rsa_mgf1_md(pkey_ctx, scheme->scheme_md()) != 1) {
                ret = Ztls_ERROR_LIBRARY;
                goto Exit;
            }
        }
        if (EVP_DigestVerifyUpdate(ctx, data.data(), data.length()) != 1) {
            ret = Ztls_ERROR_LIBRARY;
            goto Exit;
        }
        if (EVP_DigestVerifyFinal(ctx, signature.data(), signature.length()) != 1) {
            ret = Ztls_ALERT_DECRYPT_ERROR;
            goto Exit;
        }
    }

    ret = 0;

Exit:
    if (ctx != NULL)
        EVP_MD_CTX_destroy(ctx);
    EVP_PKEY_free(key);
    return ret;
}

int openssl_init_sign_certificate(openssl_sign_certificate_t *self, EVP_PKEY *key)
{
    *self = (openssl_sign_certificate_t){.super = {sign_certificate}, .async = 0 /* libssl has it off by default too */};

    if ((self->schemes = openssl_lookup_signature_schemes(key)) == NULL)
        return Ztls_ERROR_INCOMPATIBLE_KEY;
    EVP_PKEY_up_ref(key);
    self->key = key;

    return 0;
}

void openssl_dispose_sign_certificate(openssl_sign_certificate_t *self)
{
    EVP_PKEY_free(self->key);
}

static int verify_cert_chain(X509_STORE *store, X509 *cert, STACK_OF(X509) * chain, int is_server, const char *server_name,
                             int *ossl_x509_err)
{
    X509_STORE_CTX *verify_ctx;
    int ret;
    *ossl_x509_err = 0;

    /* verify certificate chain */
    if ((verify_ctx = X509_STORE_CTX_new()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }
    if (X509_STORE_CTX_init(verify_ctx, store, cert, chain) != 1) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }

    { /* setup verify params */
        X509_VERIFY_PARAM *params = X509_STORE_CTX_get0_param(verify_ctx);
        X509_VERIFY_PARAM_set_purpose(params, is_server ? X509_PURPOSE_SSL_CLIENT : X509_PURPOSE_SSL_SERVER);
        X509_VERIFY_PARAM_set_depth(params, 98); /* use the default of OpenSSL 1.0.2 and above; see `man SSL_CTX_set_verify` */
        /* when _acting_ as client, set the server name if provided*/
        if (!is_server && server_name != NULL) {
            if (server_name_is_ipaddr(server_name)) {
                X509_VERIFY_PARAM_set1_ip_asc(params, server_name);
            } else {
                X509_VERIFY_PARAM_set1_host(params, server_name, strlen(server_name));
                X509_VERIFY_PARAM_set_hostflags(params, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            }
        }
    }

    if (X509_verify_cert(verify_ctx) != 1) {
        *ossl_x509_err = X509_STORE_CTX_get_error(verify_ctx);
        switch (*ossl_x509_err) {
        case X509_V_ERR_OUT_OF_MEM:
            ret = Ztls_ERROR_NO_MEMORY;
            break;
        case X509_V_ERR_CERT_REVOKED:
            ret = Ztls_ALERT_CERTIFICATE_REVOKED;
            break;
        case X509_V_ERR_CERT_NOT_YET_VALID:
        case X509_V_ERR_CERT_HAS_EXPIRED:
            ret = Ztls_ALERT_CERTIFICATE_EXPIRED;
            break;
        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
        case X509_V_ERR_CERT_UNTRUSTED:
        case X509_V_ERR_CERT_REJECTED:
            ret = Ztls_ALERT_UNKNOWN_CA;
            break;
        case X509_V_ERR_HOSTNAME_MISMATCH:
        case X509_V_ERR_INVALID_CA:
#ifdef X509_V_ERR_CA_MD_TOO_WEAK
        case X509_V_ERR_CA_MD_TOO_WEAK:
#endif
#ifdef X509_V_ERR_UNSUPPORTED_SIGNATURE_ALGORITHM
        case X509_V_ERR_UNSUPPORTED_SIGNATURE_ALGORITHM:
#endif
            ret = Ztls_ALERT_BAD_CERTIFICATE;
            break;
        default:
            ret = Ztls_ALERT_CERTIFICATE_UNKNOWN;
            break;
        }
        goto Exit;
    }

    ret = 0;

Exit:
    if (verify_ctx != NULL)
        X509_STORE_CTX_free(verify_ctx);
    return ret;
}

static int verify_cert(verify_certificate_t *_self, Ztls::Cxn *tls, const char *server_name,
                       int (**verifier)(void *, uint16_t, Bytes, Bytes), void **verify_data, Bytes *certs,
                       size_t num_certs)
{
    openssl_verify_certificate_t *self = (openssl_verify_certificate_t *)_self;
    X509 *cert = NULL;
    STACK_OF(X509) *chain = sk_X509_new_null();
    size_t i;
    int ossl_x509_err, ret;

    /* If any certs are given, convert them to OpenSSL representation, then verify the cert chain. If no certs are given, just give
     * the override_callback to see if we want to stay fail open. */
    if (num_certs != 0) {
        if ((cert = to_x509(certs[0])) == NULL) {
            ret = Ztls_ALERT_BAD_CERTIFICATE;
            goto Exit;
        }
        for (i = 1; i != num_certs; ++i) {
            X509 *interm = to_x509(certs[i]);
            if (interm == NULL) {
                ret = Ztls_ALERT_BAD_CERTIFICATE;
                goto Exit;
            }
            sk_X509_push(chain, interm);
        }
        ret = verify_cert_chain(self->cert_store, cert, chain, is_server(tls), server_name, &ossl_x509_err);
    } else {
        ret = Ztls_ALERT_CERTIFICATE_REQUIRED;
        ossl_x509_err = 0;
    }

    /* When override callback is available, let it override the error. */
    if (self->override_callback != NULL)
        ret = self->override_callback->cb(self->override_callback, tls, ret, ossl_x509_err, cert, chain);

    if (ret != 0 || num_certs == 0)
        goto Exit;

    /* extract public key for verifying the TLS handshake signature */
    if ((*verify_data = X509_get_pubkey(cert)) == NULL) {
        ret = Ztls_ALERT_BAD_CERTIFICATE;
        goto Exit;
    }
    *verifier = verify_sign;

Exit:
    if (chain != NULL)
        sk_X509_pop_free(chain, X509_free);
    if (cert != NULL)
        X509_free(cert);
    return ret;
}

int openssl_init_verify_certificate(openssl_verify_certificate_t *self, X509_STORE *store)
{
    *self = (openssl_verify_certificate_t){{verify_cert, default_signature_schemes}, NULL};

    if (store != NULL) {
        X509_STORE_up_ref(store);
        self->cert_store = store;
    } else {
        /* use default store */
        if ((self->cert_store = openssl_create_default_certificate_store()) == NULL)
            return -1;
    }

    return 0;
}

void openssl_dispose_verify_certificate(openssl_verify_certificate_t *self)
{
    X509_STORE_free(self->cert_store);
}

X509_STORE *openssl_create_default_certificate_store(void)
{
    X509_STORE *store;
    X509_LOOKUP *lookup;

    if ((store = X509_STORE_new()) == NULL)
        goto Error;
    if ((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file())) == NULL)
        goto Error;
    X509_LOOKUP_load_file(lookup, NULL, X509_FILETYPE_DEFAULT);
    if ((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_hash_dir())) == NULL)
        goto Error;
    X509_LOOKUP_add_dir(lookup, NULL, X509_FILETYPE_DEFAULT);

    return store;
Error:
    if (store != NULL)
        X509_STORE_free(store);
    return NULL;
}

static int verify_raw_cert(verify_certificate_t *_self, Ztls::Cxn *tls, const char *server_name,
                           int (**verifier)(void *, uint16_t algo, Bytes, Bytes), void **verify_data,
                           Bytes *certs, size_t num_certs)
{
    openssl_raw_pubkey_verify_certificate_t *self = (openssl_raw_pubkey_verify_certificate_t *)_self;
    int ret = Ztls_ALERT_BAD_CERTIFICATE;
    uint8_t *encoded_key = nullptr;

    int r;

    assert(num_certs != 0);

    if (num_certs != 1)
        goto Exit;

    r = i2d_PUBKEY(self->expected_pubkey, &encoded_key);
    if (r <= 0) {
        ret = Ztls_ALERT_BAD_CERTIFICATE;
        goto Exit;
    }

    if (!Ztls::ctEqual({encoded_key, size_t(r)}, {certs[0].data(), certs[0].length()}))
        goto Exit;

    EVP_PKEY_up_ref(self->expected_pubkey);
    *verify_data = self->expected_pubkey;
    *verifier = verify_sign;
    ret = 0;
Exit:
    OPENSSL_free(encoded_key);
    return ret;
}

int openssl_raw_pubkey_init_verify_certificate(openssl_raw_pubkey_verify_certificate_t *self, EVP_PKEY *expected_pubkey)
{
    EVP_PKEY_up_ref(expected_pubkey);
    *self = (openssl_raw_pubkey_verify_certificate_t){{verify_raw_cert, default_signature_schemes}, expected_pubkey};
    return 0;
}
void openssl_raw_pubkey_dispose_verify_certificate(openssl_raw_pubkey_verify_certificate_t *self)
{
    EVP_PKEY_free(self->expected_pubkey);
}

#define TICKET_LABEL_SIZE 16
#define TICKET_IV_SIZE EVP_MAX_IV_LENGTH

int openssl_encrypt_ticket(buffer_t *buf, Bytes src,
                                int (*cb)(unsigned char *key_name, unsigned char *iv, EVP_CIPHER_CTX *ctx, HMAC_CTX *hctx, int enc))
{
    EVP_CIPHER_CTX *cctx = NULL;
    HMAC_CTX *hctx = NULL;
    uint8_t *dst;
    int clen, ret;

    if ((cctx = EVP_CIPHER_CTX_new()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }
    if ((hctx = HMAC_CTX_new()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }

    if ((ret = buffer_reserve(buf, TICKET_LABEL_SIZE + TICKET_IV_SIZE + src.length() + EVP_MAX_BLOCK_LENGTH + EVP_MAX_MD_SIZE,
                                   buf->tx)) !=
        0)
        goto Exit;
    dst = buf->base + buf->off;

    /* fill label and iv, as well as obtaining the keys */
    if (!(*cb)(dst, dst + TICKET_LABEL_SIZE, cctx, hctx, 1)) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    dst += TICKET_LABEL_SIZE + TICKET_IV_SIZE;

    /* encrypt */
    if (!EVP_EncryptUpdate(cctx, dst, &clen, src.data(), (int)src.length())) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    dst += clen;
    if (!EVP_EncryptFinal_ex(cctx, dst, &clen)) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    dst += clen;

    /* append hmac */
    if (!HMAC_Update(hctx, buf->base + buf->off, dst - (buf->base + buf->off)) || !HMAC_Final(hctx, dst, NULL)) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    dst += HMAC_size(hctx);

    assert(dst <= buf->base + buf->capacity);
    buf->off += dst - (buf->base + buf->off);
    ret = 0;

Exit:
    if (cctx != NULL)
        EVP_CIPHER_CTX_free(cctx);
    if (hctx != NULL)
        HMAC_CTX_free(hctx);
    return ret;
}

int openssl_decrypt_ticket(buffer_t *buf, Bytes src,
                                int (*cb)(unsigned char *key_name, unsigned char *iv, EVP_CIPHER_CTX *ctx, HMAC_CTX *hctx, int enc))
{
    size_t hmac_size;
    EVP_CIPHER_CTX *cctx = NULL;
    HMAC_CTX *hctx = NULL;
    int clen, ret;

    if ((cctx = EVP_CIPHER_CTX_new()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }
    if ((hctx = HMAC_CTX_new()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }

    /* obtain cipher and hash context.
     * Note: no need to handle renew, since the server always sends a new ticket to minimize the chance of ticket reuse */
    if (src.length() < TICKET_LABEL_SIZE + TICKET_IV_SIZE) {
        ret = Ztls_ALERT_DECODE_ERROR;
        goto Exit;
    }
    if (!(*cb)(src.data(), src.data() + TICKET_LABEL_SIZE, cctx, hctx, 0)) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }

    /* check hmac, and exclude label, iv, hmac */
    hmac_size = HMAC_size(hctx);
    if (src.length() < TICKET_LABEL_SIZE + TICKET_IV_SIZE + hmac_size) {
        ret = Ztls_ALERT_DECODE_ERROR;
        goto Exit;
    }
    src = {src.data(), src.length() - hmac_size};
    uint8_t hmac[EVP_MAX_MD_SIZE];
    if (!HMAC_Update(hctx, src.data(), src.length()) || !HMAC_Final(hctx, hmac, NULL)) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (!Ztls::ctEqual({src.data() + src.length(), hmac_size}, {hmac, hmac_size})) {
        ret = Ztls_ALERT_HANDSHAKE_FAILURE;
        goto Exit;
    }
    src = {src.data() + TICKET_LABEL_SIZE + TICKET_IV_SIZE, src.length() - TICKET_LABEL_SIZE - TICKET_IV_SIZE};

    /* decrypt */
    if ((ret = buffer_reserve(buf, src.length(), buf->tx)) != 0)
        goto Exit;
    if (!EVP_DecryptUpdate(cctx, buf->base + buf->off, &clen, src.data(), (int)src.length())) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    buf->off += clen;
    if (!EVP_DecryptFinal_ex(cctx, buf->base + buf->off, &clen)) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    buf->off += clen;

    ret = 0;

Exit:
    if (cctx != NULL)
        EVP_CIPHER_CTX_free(cctx);
    if (hctx != NULL)
        HMAC_CTX_free(hctx);
    return ret;
}

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
int openssl_encrypt_ticket_evp(buffer_t *buf, Bytes src,
                                    int (*cb)(unsigned char *key_name, unsigned char *iv, EVP_CIPHER_CTX *ctx, EVP_MAC_CTX *hctx,
                                              int enc))
{
    EVP_CIPHER_CTX *cctx = NULL;
    EVP_MAC *mac = NULL;
    EVP_MAC_CTX *hctx = NULL;
    size_t hlen;
    uint8_t *dst;
    int clen, ret;

    if ((cctx = EVP_CIPHER_CTX_new()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }
    if ((mac = EVP_MAC_fetch(NULL, "HMAC", NULL)) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }
    if ((hctx = EVP_MAC_CTX_new(mac)) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }

    if ((ret = buffer_reserve(buf, TICKET_LABEL_SIZE + TICKET_IV_SIZE + src.length() + EVP_MAX_BLOCK_LENGTH + EVP_MAX_MD_SIZE,
                                   buf->tx)) !=
        0)
        goto Exit;
    dst = buf->base + buf->off;

    /* fill label and iv, as well as obtaining the keys */
    if (!(*cb)(dst, dst + TICKET_LABEL_SIZE, cctx, hctx, 1)) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    dst += TICKET_LABEL_SIZE + TICKET_IV_SIZE;

    /* encrypt */
    if (!EVP_EncryptUpdate(cctx, dst, &clen, src.data(), (int)src.length())) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    dst += clen;
    if (!EVP_EncryptFinal_ex(cctx, dst, &clen)) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    dst += clen;

    /* append hmac */
    if (!EVP_MAC_update(hctx, buf->base + buf->off, dst - (buf->base + buf->off)) ||
        !EVP_MAC_final(hctx, dst, &hlen, EVP_MAC_CTX_get_mac_size(hctx))) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    dst += hlen;

    assert(dst <= buf->base + buf->capacity);
    buf->off += dst - (buf->base + buf->off);
    ret = 0;

Exit:
    if (cctx != NULL)
        EVP_CIPHER_CTX_free(cctx);
    if (hctx != NULL)
        EVP_MAC_CTX_free(hctx);
    if (mac != NULL)
        EVP_MAC_free(mac);
    return ret;
}

int openssl_decrypt_ticket_evp(buffer_t *buf, Bytes src,
                                    int (*cb)(unsigned char *key_name, unsigned char *iv, EVP_CIPHER_CTX *ctx, EVP_MAC_CTX *hctx,
                                              int enc))
{
    size_t hmac_size;
    EVP_CIPHER_CTX *cctx = NULL;
    EVP_MAC *mac = NULL;
    EVP_MAC_CTX *hctx = NULL;
    size_t hlen;
    int clen, ret;

    if ((cctx = EVP_CIPHER_CTX_new()) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }
    if ((mac = EVP_MAC_fetch(NULL, "HMAC", NULL)) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }
    if ((hctx = EVP_MAC_CTX_new(mac)) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }

    /* obtain cipher and hash context.
     * Note: no need to handle renew, since the server always sends a new ticket to minimize the chance of ticket reuse */
    if (src.length() < TICKET_LABEL_SIZE + TICKET_IV_SIZE) {
        ret = Ztls_ALERT_DECODE_ERROR;
        goto Exit;
    }
    if (!(*cb)(src.data(), src.data() + TICKET_LABEL_SIZE, cctx, hctx, 0)) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }

    /* check hmac, and exclude label, iv, hmac */
    hmac_size = EVP_MAC_CTX_get_mac_size(hctx);
    if (src.length() < TICKET_LABEL_SIZE + TICKET_IV_SIZE + hmac_size) {
        ret = Ztls_ALERT_DECODE_ERROR;
        goto Exit;
    }
    src = {src.data(), src.length() - hmac_size};
    uint8_t hmac[EVP_MAX_MD_SIZE];
    if (!EVP_MAC_update(hctx, src.data(), src.length()) || !EVP_MAC_final(hctx, hmac, &hlen, sizeof(hmac))) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    if (!Ztls::ctEqual({src.data() + src.length(), hmac_size}, {hmac, hmac_size})) {
        ret = Ztls_ALERT_HANDSHAKE_FAILURE;
        goto Exit;
    }
    src = {src.data() + TICKET_LABEL_SIZE + TICKET_IV_SIZE, src.length() - TICKET_LABEL_SIZE - TICKET_IV_SIZE};

    /* decrypt */
    if ((ret = buffer_reserve(buf, src.length(), buf->tx)) != 0)
        goto Exit;
    if (!EVP_DecryptUpdate(cctx, buf->base + buf->off, &clen, src.data(), (int)src.length())) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    buf->off += clen;
    if (!EVP_DecryptFinal_ex(cctx, buf->base + buf->off, &clen)) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    buf->off += clen;

    ret = 0;

Exit:
    if (cctx != NULL)
        EVP_CIPHER_CTX_free(cctx);
    if (hctx != NULL)
        EVP_MAC_CTX_free(hctx);
    if (mac != NULL)
        EVP_MAC_free(mac);
    return ret;
}
#endif

key_exchange_algorithm_t openssl_secp256r1 = {.id = Ztls_GROUP_SECP256R1,
                                                        .create = x9_62_create_key_exchange,
                                                        .exchange = secp_key_exchange,
                                                        .data = NID_X9_62_prime256v1,
                                                        .name = Ztls_GROUP_NAME_SECP256R1};
#if Ztls_OPENSSL_HAVE_SECP384R1
key_exchange_algorithm_t openssl_secp384r1 = {.id = Ztls_GROUP_SECP384R1,
                                                        .create = x9_62_create_key_exchange,
                                                        .exchange = secp_key_exchange,
                                                        .data = NID_secp384r1,
                                                        .name = Ztls_GROUP_NAME_SECP384R1};
#endif
#if Ztls_OPENSSL_HAVE_SECP521R1
key_exchange_algorithm_t openssl_secp521r1 = {.id = Ztls_GROUP_SECP521R1,
                                                        .create = x9_62_create_key_exchange,
                                                        .exchange = secp_key_exchange,
                                                        .data = NID_secp521r1,
                                                        .name = Ztls_GROUP_NAME_SECP521R1};
#endif
#if Ztls_OPENSSL_HAVE_X25519
key_exchange_algorithm_t openssl_x25519 = {.id = Ztls_GROUP_X25519,
                                                     .create = evp_keyex_create,
                                                     .exchange = evp_keyex_exchange,
                                                     .data = NID_X25519,
                                                     .name = Ztls_GROUP_NAME_X25519};
#endif
#if defined(OPENSSL_IS_BORINGSSL) && Ztls_OPENSSL_HAVE_X25519MLKEM768
key_exchange_algorithm_t openssl_x25519mlkem768 = {.id = Ztls_GROUP_X25519MLKEM768,
                                                             .create = x25519mlkem768_create,
                                                             .exchange = x25519mlkem768_exchange,
                                                             .name = Ztls_GROUP_NAME_X25519MLKEM768};
#endif
#if Ztls_OPENSSL_HAVE_MLKEM
#define DEFINE_MLKEM_KEYEX(lowcase, upcase) \
    key_exchange_algorithm_t openssl_##lowcase = { \
        .id = Ztls_GROUP_##upcase, \
        .create = evp_kem_create, \
        .exchange = evp_kem_exchange, \
        .data = (intptr_t)Ztls_GROUP_NAME_##upcase, \
        .name = Ztls_GROUP_NAME_##upcase}
DEFINE_MLKEM_KEYEX(x25519mlkem768, X25519MLKEM768);
DEFINE_MLKEM_KEYEX(secp256r1mlkem768, SECP256R1MLKEM768);
DEFINE_MLKEM_KEYEX(secp384r1mlkem1024, SECP384R1MLKEM1024);
DEFINE_MLKEM_KEYEX(mlkem512, MLKEM512);
DEFINE_MLKEM_KEYEX(mlkem768, MLKEM768);
DEFINE_MLKEM_KEYEX(mlkem1024, MLKEM1024);
#undef DEFINE_MLKEM_KEYEX
#endif

key_exchange_algorithm_t *openssl_key_exchanges[] = {&openssl_secp256r1, NULL};
key_exchange_algorithm_t *openssl_key_exchanges_all[] = {
#if Ztls_OPENSSL_HAVE_X25519MLKEM768
    &openssl_x25519mlkem768,
#endif
#if Ztls_OPENSSL_HAVE_SECP521R1
    &openssl_secp521r1,
#endif
#if Ztls_OPENSSL_HAVE_SECP384R1
    &openssl_secp384r1,
#endif
#if Ztls_OPENSSL_HAVE_X25519
    &openssl_x25519,
#endif
#if Ztls_OPENSSL_HAVE_MLKEM
    &openssl_secp384r1mlkem1024,
    &openssl_secp256r1mlkem768,
    &openssl_mlkem1024,
    &openssl_mlkem768,
    &openssl_mlkem512,
#endif
    &openssl_secp256r1,
    NULL};
cipher_algorithm_t openssl_aes128ecb = {
    "AES128-ECB",          Ztls_AES128_KEY_SIZE, Ztls_AES_BLOCK_SIZE, 0 /* iv size */, sizeof(struct openssl_cipher_context_t),
    aes128ecb_setup_crypto};
cipher_algorithm_t openssl_aes128ctr = {
    "AES128-CTR", Ztls_AES128_KEY_SIZE, 1, Ztls_AES_IV_SIZE, sizeof(struct openssl_cipher_context_t), aes128ctr_setup_crypto};
aead_algorithm_t openssl_aes128gcm = {"AES128-GCM",
                                                Ztls_AESGCM_CONFIDENTIALITY_LIMIT,
                                                Ztls_AESGCM_INTEGRITY_LIMIT,
                                                &openssl_aes128ctr,
                                                &openssl_aes128ecb,
                                                Ztls_AES128_KEY_SIZE,
                                                Ztls_AESGCM_IV_SIZE,
                                                Ztls_AESGCM_TAG_SIZE,

                                                0,
                                                0,
                                                sizeof(struct aead_crypto_context_t),
                                                aead_aes128gcm_setup_crypto,
                                                 1,
                                                 1};
cipher_algorithm_t openssl_aes256ecb = {
    "AES256-ECB",          Ztls_AES256_KEY_SIZE, Ztls_AES_BLOCK_SIZE, 0 /* iv size */, sizeof(struct openssl_cipher_context_t),
    aes256ecb_setup_crypto};
cipher_algorithm_t openssl_aes256ctr = {
    "AES256-CTR",          Ztls_AES256_KEY_SIZE, 1 /* block size */, Ztls_AES_IV_SIZE, sizeof(struct openssl_cipher_context_t),
    aes256ctr_setup_crypto};
aead_algorithm_t openssl_aes256gcm = {"AES256-GCM",
                                                Ztls_AESGCM_CONFIDENTIALITY_LIMIT,
                                                Ztls_AESGCM_INTEGRITY_LIMIT,
                                                &openssl_aes256ctr,
                                                &openssl_aes256ecb,
                                                Ztls_AES256_KEY_SIZE,
                                                Ztls_AESGCM_IV_SIZE,
                                                Ztls_AESGCM_TAG_SIZE,

                                                0,
                                                0,
                                                sizeof(struct aead_crypto_context_t),
                                                aead_aes256gcm_setup_crypto,
                                                 1,
                                                 1};
hash_algorithm_t openssl_sha256 = {"sha256", Ztls_SHA256_BLOCK_SIZE, Ztls_SHA256_DIGEST_SIZE, sha256_create,
                                             Ztls_ZERO_DIGEST_SHA256};
hash_algorithm_t openssl_sha384 = {"sha384", Ztls_SHA384_BLOCK_SIZE, Ztls_SHA384_DIGEST_SIZE, sha384_create,
                                             Ztls_ZERO_DIGEST_SHA384};
hash_algorithm_t openssl_sha512 = {"sha512", Ztls_SHA512_BLOCK_SIZE, Ztls_SHA512_DIGEST_SIZE, sha512_create,
                                             Ztls_ZERO_DIGEST_SHA512};
cipher_suite_t openssl_aes128gcmsha256 = {.id = Ztls_CIPHER_SUITE_AES_128_GCM_SHA256,
                                                    .aead = &openssl_aes128gcm,
                                                    .hash = &openssl_sha256,
                                                    .name = Ztls_CIPHER_SUITE_NAME_AES_128_GCM_SHA256};
cipher_suite_t openssl_aes256gcmsha384 = {.id = Ztls_CIPHER_SUITE_AES_256_GCM_SHA384,
                                                    .aead = &openssl_aes256gcm,
                                                    .hash = &openssl_sha384,
                                                    .name = Ztls_CIPHER_SUITE_NAME_AES_256_GCM_SHA384};
#if Ztls_OPENSSL_HAVE_CHACHA20_POLY1305
cipher_algorithm_t openssl_chacha20 = {
    .name = "CHACHA20",
    .key_size = Ztls_CHACHA20_KEY_SIZE,
    .block_size = 1,
    .iv_size = Ztls_CHACHA20_IV_SIZE,
#ifdef OPENSSL_IS_BORINGSSL
    .context_size = sizeof(struct boringssl_chacha20_context_t),
    .setup_crypto = boringssl_chacha20_setup_crypto,
#else
    .context_size = sizeof(struct openssl_cipher_context_t),
    .setup_crypto = chacha20_setup_crypto,
#endif
};
aead_algorithm_t openssl_chacha20poly1305 = {
    .name = "CHACHA20-POLY1305",
    .confidentiality_limit = Ztls_CHACHA20POLY1305_CONFIDENTIALITY_LIMIT,
    .integrity_limit = Ztls_CHACHA20POLY1305_INTEGRITY_LIMIT,
    .ctr_cipher = &openssl_chacha20,
    .ecb_cipher = NULL,
    .key_size = Ztls_CHACHA20_KEY_SIZE,
    .iv_size = Ztls_CHACHA20POLY1305_IV_SIZE,
    .tag_size = Ztls_CHACHA20POLY1305_TAG_SIZE,

    .non_temporal = 0,
    .align_bits = 0,
#ifdef OPENSSL_IS_BORINGSSL
    .context_size = sizeof(struct boringssl_chacha20poly1305_context_t),
    .setup_crypto = boringssl_chacha20poly1305_setup_crypto,
#else
    .context_size = sizeof(struct aead_crypto_context_t),
    .setup_crypto = aead_chacha20poly1305_setup_crypto,
    .serial_encrypt = 1,
    .serial_decrypt = 1,
#endif
};
cipher_suite_t openssl_chacha20poly1305sha256 = {.id = Ztls_CIPHER_SUITE_CHACHA20_POLY1305_SHA256,
                                                           .aead = &openssl_chacha20poly1305,
                                                           .hash = &openssl_sha256,
                                                           .name = Ztls_CIPHER_SUITE_NAME_CHACHA20_POLY1305_SHA256};
#endif

#if Ztls_HAVE_AEGIS
aead_algorithm_t openssl_aegis128l = {
    .name = "AEGIS-128L",
    .confidentiality_limit = Ztls_AEGIS128L_CONFIDENTIALITY_LIMIT,
    .integrity_limit = Ztls_AEGIS128L_INTEGRITY_LIMIT,
    .ctr_cipher = NULL,
    .ecb_cipher = NULL,
    .key_size = Ztls_AEGIS128L_KEY_SIZE,
    .iv_size = Ztls_AEGIS128L_IV_SIZE,
    .tag_size = Ztls_AEGIS128L_TAG_SIZE,

    .non_temporal = 0,
    .align_bits = 0,
    .context_size = sizeof(struct aegis128l_context_t),
    .setup_crypto = aegis128l_setup_crypto,
};
cipher_suite_t openssl_aegis128lsha256 = {.id = Ztls_CIPHER_SUITE_AEGIS128L_SHA256,
                                                    .aead = &openssl_aegis128l,
                                                    .hash = &openssl_sha256,
                                                    .name = Ztls_CIPHER_SUITE_NAME_AEGIS128L_SHA256};

aead_algorithm_t openssl_aegis256 = {
    .name = "AEGIS-256",
    .confidentiality_limit = Ztls_AEGIS256_CONFIDENTIALITY_LIMIT,
    .integrity_limit = Ztls_AEGIS256_INTEGRITY_LIMIT,
    .ctr_cipher = NULL,
    .ecb_cipher = NULL,
    .key_size = Ztls_AEGIS256_KEY_SIZE,
    .iv_size = Ztls_AEGIS256_IV_SIZE,
    .tag_size = Ztls_AEGIS256_TAG_SIZE,

    .non_temporal = 0,
    .align_bits = 0,
    .context_size = sizeof(struct aegis256_context_t),
    .setup_crypto = aegis256_setup_crypto,
};
cipher_suite_t openssl_aegis256sha512 = {.id = Ztls_CIPHER_SUITE_AEGIS256_SHA512,
                                                   .aead = &openssl_aegis256,
                                                   .hash = &openssl_sha512,
                                                   .name = Ztls_CIPHER_SUITE_NAME_AEGIS256_SHA512};
#endif

cipher_suite_t *openssl_cipher_suites[] = { // ciphers used with sha384 (must be first)
    &openssl_aes256gcmsha384,

    // ciphers used with sha256
    &openssl_aes128gcmsha256,
#if Ztls_OPENSSL_HAVE_CHACHA20_POLY1305
    &openssl_chacha20poly1305sha256,
#endif
    NULL};

cipher_suite_t *openssl_cipher_suites_all[] = { // ciphers used with sha384 (must be first)
#if Ztls_HAVE_AEGIS
    &openssl_aegis256sha512,
#endif
    &openssl_aes256gcmsha384,

// ciphers used with sha256
#if Ztls_HAVE_AEGIS
    &openssl_aegis128lsha256,
#endif
    &openssl_aes128gcmsha256,
#if Ztls_OPENSSL_HAVE_CHACHA20_POLY1305
    &openssl_chacha20poly1305sha256,
#endif
    NULL};


#if Ztls_OPENSSL_HAVE_BF
cipher_algorithm_t openssl_bfecb = {"BF-ECB",        Ztls_BLOWFISH_KEY_SIZE,          Ztls_BLOWFISH_BLOCK_SIZE,
                                              0 /* iv size */, sizeof(struct openssl_cipher_context_t), bfecb_setup_crypto};
#endif


hpke_kem_t openssl_hpke_kem_p256sha256 = {Ztls_HPKE_KEM_P256_SHA256, &openssl_secp256r1, &openssl_sha256};
hpke_kem_t openssl_hpke_kem_p384sha384 = {Ztls_HPKE_KEM_P384_SHA384, &openssl_secp384r1, &openssl_sha384};
#if Ztls_OPENSSL_HAVE_X25519
hpke_kem_t openssl_hpke_kem_x25519sha256 = {Ztls_HPKE_KEM_X25519_SHA256, &openssl_x25519, &openssl_sha256};
#endif
hpke_kem_t *openssl_hpke_kems[] = {&openssl_hpke_kem_p384sha384,
#if Ztls_OPENSSL_HAVE_X25519
                                             &openssl_hpke_kem_x25519sha256,
#endif
                                             &openssl_hpke_kem_p256sha256, NULL};

hpke_cipher_suite_t openssl_hpke_aes128gcmsha256 = {
    .id = {.kdf = Ztls_HPKE_HKDF_SHA256, .aead = Ztls_HPKE_AEAD_AES_128_GCM},
    .name = "HKDF-SHA256/AES-128-GCM",
    .hash = &openssl_sha256,
    .aead = &openssl_aes128gcm};
hpke_cipher_suite_t openssl_hpke_aes128gcmsha512 = {
    .id = {.kdf = Ztls_HPKE_HKDF_SHA512, .aead = Ztls_HPKE_AEAD_AES_128_GCM},
    .name = "HKDF-SHA512/AES-128-GCM",
    .hash = &openssl_sha512,
    .aead = &openssl_aes128gcm};
hpke_cipher_suite_t openssl_hpke_aes256gcmsha384 = {
    .id = {.kdf = Ztls_HPKE_HKDF_SHA384, .aead = Ztls_HPKE_AEAD_AES_256_GCM},
    .name = "HKDF-SHA384/AES-256-GCM",
    .hash = &openssl_sha384,
    .aead = &openssl_aes256gcm};
#if Ztls_OPENSSL_HAVE_CHACHA20_POLY1305
hpke_cipher_suite_t openssl_hpke_chacha20poly1305sha256 = {
    .id = {.kdf = Ztls_HPKE_HKDF_SHA256, .aead = Ztls_HPKE_AEAD_CHACHA20POLY1305},
    .name = "HKDF-SHA256/ChaCha20Poly1305",
    .hash = &openssl_sha256,
    .aead = &openssl_chacha20poly1305};
#endif
hpke_cipher_suite_t *openssl_hpke_cipher_suites[] = {&openssl_hpke_aes128gcmsha256,
                                                               &openssl_hpke_aes256gcmsha384,
#if Ztls_OPENSSL_HAVE_CHACHA20_POLY1305
                                                               &openssl_hpke_chacha20poly1305sha256,
#endif
                                                               &openssl_hpke_aes128gcmsha512, /* likely only for tests */
                                                               NULL};

} // Ztls_
