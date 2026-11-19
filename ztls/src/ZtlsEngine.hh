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
#ifndef ZtlsEngine_HH
#define ZtlsEngine_HH

#include <zlib/ZtlsLib.hh>
#include <zlib/ZuLib.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZtArray.hh>

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifndef Ztls_Fusion
#if defined(__x86_64__) || defined(_M_X64) || \
    defined(__i386__) || defined(_M_IX86)
#define Ztls_Fusion 1
#else
#define Ztls_Fusion 0
#endif
#endif

namespace Ztls { class Cxn; class RecordSink; }

namespace Ztls_ {

ZuDerive(CertificateDER, (ZtBArray<ZtArrayHeapID<"Ztls.Engine.CertificateDER">>));
ZuDerive(CertificateChain, (ZtArray<CertificateDER, ZtArrayHeapID<"Ztls.Engine.CertificateChain">>));

ZuDerive(KeyExchangeBytes, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.KeyExchange">>>));
ZuDerive(EchBytes, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.ECH">>>));

#if __GNUC__ >= 3
#define Ztls_LIKELY(x) __builtin_expect(!!(x), 1)
#define Ztls_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define Ztls_BUILD_ASSERT_EXPR(cond) (sizeof(char[2 * !!(!__builtin_constant_p(cond) || (cond)) - 1]) != 0)
#define Ztls_BUILD_ASSERT(cond) ((void)Ztls_BUILD_ASSERT_EXPR(cond))
#else
#define Ztls_LIKELY(x) (x)
#define Ztls_UNLIKELY(x) (x)
#define Ztls_BUILD_ASSERT(cond) 1
#endif

/* __builtin_types_compatible_p yields incorrect results when older versions of GCC is used; see #303.
 * Clang with Xcode 9.4 or prior is known to not work correctly when a pointer is const-qualified; see
 * https://github.com/h2o/quicly/pull/306#issuecomment-626037269. Older versions of clang upstream works fine, but we do not need
 * best coverage. This macro is for preventing misuse going into the master branch, having it work one of the compilers supported in
 * our CI is enough.
 */
#if ((defined(__clang__) && __clang_major__ >= 10) || __GNUC__ >= 6) && !defined(__cplusplus)
#define Ztls_ASSERT_IS_ARRAY_EXPR(a) Ztls_BUILD_ASSERT_EXPR(__builtin_types_compatible_p(__typeof__(a[0])[], __typeof__(a)))
#else
#define Ztls_ASSERT_IS_ARRAY_EXPR(a) 1
#endif

#define Ztls_ELEMENTSOF(x) (Ztls_ASSERT_IS_ARRAY_EXPR(x) * sizeof(x) / sizeof((x)[0]))

#define Ztls_HELLO_RANDOM_SIZE 32

#define Ztls_AES128_KEY_SIZE 16
#define Ztls_AES256_KEY_SIZE 32
#define Ztls_AES_BLOCK_SIZE 16
#define Ztls_AES_IV_SIZE 16
#define Ztls_AESGCM_IV_SIZE 12
#define Ztls_AESGCM_TAG_SIZE 16
#define Ztls_AESGCM_CONFIDENTIALITY_LIMIT 0x2000000            /* 2^25 */
#define Ztls_AESGCM_INTEGRITY_LIMIT UINT64_C(0x40000000000000) /* 2^54 */
#define Ztls_AESCCM_CONFIDENTIALITY_LIMIT 0xB504F3             /* 2^23.5 */
#define Ztls_AESCCM_INTEGRITY_LIMIT 0xB504F3                   /* 2^23.5 */

#define Ztls_CHACHA20_KEY_SIZE 32
#define Ztls_CHACHA20_IV_SIZE 16 /* contrary to RFC 7539, follow OpenSSL way of using first 32 bits as ctr and latter 96 as IV */
#define Ztls_CHACHA20POLY1305_IV_SIZE 12
#define Ztls_CHACHA20POLY1305_TAG_SIZE 16
#define Ztls_CHACHA20POLY1305_CONFIDENTIALITY_LIMIT UINT64_MAX       /* at least 2^64 */
#define Ztls_CHACHA20POLY1305_INTEGRITY_LIMIT UINT64_C(0x1000000000) /* 2^36 */

#define Ztls_AEGIS128L_KEY_SIZE 16
#define Ztls_AEGIS128L_IV_SIZE 16
#define Ztls_AEGIS128L_TAG_SIZE 16
#define Ztls_AEGIS128L_CONFIDENTIALITY_LIMIT UINT64_MAX          /* at least 2^64 */
#define Ztls_AEGIS128L_INTEGRITY_LIMIT UINT64_C(0x1000000000000) /* 2^48 */

#define Ztls_AEGIS256_KEY_SIZE 32
#define Ztls_AEGIS256_IV_SIZE 32
#define Ztls_AEGIS256_TAG_SIZE 16
#define Ztls_AEGIS256_CONFIDENTIALITY_LIMIT UINT64_MAX          /* at least 2^64 */
#define Ztls_AEGIS256_INTEGRITY_LIMIT UINT64_C(0x1000000000000) /* 2^48 */

#define Ztls_BLOWFISH_KEY_SIZE 16
#define Ztls_BLOWFISH_BLOCK_SIZE 8

#define Ztls_SHA256_BLOCK_SIZE 64
#define Ztls_SHA256_DIGEST_SIZE 32

#define Ztls_SHA384_BLOCK_SIZE 128
#define Ztls_SHA384_DIGEST_SIZE 48

#define Ztls_SHA512_BLOCK_SIZE 128
#define Ztls_SHA512_DIGEST_SIZE 64

#define Ztls_MAX_SECRET_SIZE 32
#define Ztls_MAX_IV_SIZE 32
#define Ztls_MAX_DIGEST_SIZE 64
#define Ztls_MAX_SIGNATURE_ALGORITHMS 64

/* versions */
#define Ztls_PROTOCOL_VERSION_TLS13 0x0304

/* cipher-suites */
#define Ztls_CIPHER_SUITE_AES_128_GCM_SHA256 0x1301
#define Ztls_CIPHER_SUITE_NAME_AES_128_GCM_SHA256 "TLS_AES_128_GCM_SHA256"
#define Ztls_CIPHER_SUITE_AES_256_GCM_SHA384 0x1302
#define Ztls_CIPHER_SUITE_NAME_AES_256_GCM_SHA384 "TLS_AES_256_GCM_SHA384"
#define Ztls_CIPHER_SUITE_CHACHA20_POLY1305_SHA256 0x1303
#define Ztls_CIPHER_SUITE_NAME_CHACHA20_POLY1305_SHA256 "TLS_CHACHA20_POLY1305_SHA256"
#define Ztls_CIPHER_SUITE_AEGIS256_SHA512 0x1306
#define Ztls_CIPHER_SUITE_NAME_AEGIS256_SHA512 "TLS_AEGIS_256_SHA512"
#define Ztls_CIPHER_SUITE_AEGIS128L_SHA256 0x1307
#define Ztls_CIPHER_SUITE_NAME_AEGIS128L_SHA256 "TLS_AEGIS_128L_SHA256"


/* negotiated_groups */
#define Ztls_GROUP_SECP256R1 23
#define Ztls_GROUP_NAME_SECP256R1 "secp256r1"
#define Ztls_GROUP_SECP384R1 24
#define Ztls_GROUP_NAME_SECP384R1 "secp384r1"
#define Ztls_GROUP_SECP521R1 25
#define Ztls_GROUP_NAME_SECP521R1 "secp521r1"
#define Ztls_GROUP_X25519 29
#define Ztls_GROUP_NAME_X25519 "x25519"
#define Ztls_GROUP_X448 30
#define Ztls_GROUP_NAME_X448 "x448"
#define Ztls_GROUP_SECP256R1MLKEM768 4587
#define Ztls_GROUP_NAME_SECP256R1MLKEM768 "SecP256r1MLKEM768"
#define Ztls_GROUP_X25519MLKEM768 4588
#define Ztls_GROUP_NAME_X25519MLKEM768 "X25519MLKEM768"
#define Ztls_GROUP_SECP384R1MLKEM1024 4589
#define Ztls_GROUP_NAME_SECP384R1MLKEM1024 "SecP384r1MLKEM1024"
#define Ztls_GROUP_MLKEM512 512
#define Ztls_GROUP_NAME_MLKEM512 "MLKEM512"
#define Ztls_GROUP_MLKEM768 513
#define Ztls_GROUP_NAME_MLKEM768 "MLKEM768"
#define Ztls_GROUP_MLKEM1024 514
#define Ztls_GROUP_NAME_MLKEM1024 "MLKEM1024"

/* signature algorithms */
#define Ztls_SIGNATURE_RSA_PKCS1_SHA1 0x0201
#define Ztls_SIGNATURE_RSA_PKCS1_SHA256 0x0401
#define Ztls_SIGNATURE_ECDSA_SECP256R1_SHA256 0x0403
#define Ztls_SIGNATURE_ECDSA_SECP384R1_SHA384 0x0503
#define Ztls_SIGNATURE_ECDSA_SECP521R1_SHA512 0x0603
#define Ztls_SIGNATURE_RSA_PSS_RSAE_SHA256 0x0804
#define Ztls_SIGNATURE_RSA_PSS_RSAE_SHA384 0x0805
#define Ztls_SIGNATURE_RSA_PSS_RSAE_SHA512 0x0806
#define Ztls_SIGNATURE_ED25519 0x0807

/* HPKE */
#define Ztls_HPKE_MODE_BASE 0
#define Ztls_HPKE_MODE_PSK 1
#define Ztls_HPKE_MODE_AUTH 2
#define Ztls_HPKE_MODE_AUTH_PSK 3
#define Ztls_HPKE_KEM_P256_SHA256 16
#define Ztls_HPKE_KEM_P384_SHA384 17
#define Ztls_HPKE_KEM_X25519_SHA256 32
#define Ztls_HPKE_HKDF_SHA256 1
#define Ztls_HPKE_HKDF_SHA384 2
#define Ztls_HPKE_HKDF_SHA512 3
#define Ztls_HPKE_AEAD_AES_128_GCM 1
#define Ztls_HPKE_AEAD_AES_256_GCM 2
#define Ztls_HPKE_AEAD_CHACHA20POLY1305 3

/* error classes and macros */
#define Ztls_ERROR_CLASS_SELF_ALERT 0
#define Ztls_ERROR_CLASS_PEER_ALERT 0x100
#define Ztls_ERROR_CLASS_INTERNAL 0x200

#define Ztls_ERROR_GET_CLASS(e) ((e) & ~0xff)
#define Ztls_ALERT_TO_SELF_ERROR(e) ((e) + Ztls_ERROR_CLASS_SELF_ALERT)
#define Ztls_ALERT_TO_PEER_ERROR(e) ((e) + Ztls_ERROR_CLASS_PEER_ALERT)
#define Ztls_ERROR_TO_ALERT(e) ((e) & 0xff)

/* the HKDF prefix */
#define Ztls_HKDF_EXPAND_LABEL_PREFIX "tls13 "

/* alerts */
#define Ztls_ALERT_LEVEL_WARNING 1
#define Ztls_ALERT_LEVEL_FATAL 2

#define Ztls_ALERT_CLOSE_NOTIFY 0
#define Ztls_ALERT_UNEXPECTED_MESSAGE 10
#define Ztls_ALERT_BAD_RECORD_MAC 20
#define Ztls_ALERT_RECORD_OVERFLOW 22
#define Ztls_ALERT_HANDSHAKE_FAILURE 40
#define Ztls_ALERT_BAD_CERTIFICATE 42
#define Ztls_ALERT_UNSUPPORTED_CERTIFICATE 43
#define Ztls_ALERT_CERTIFICATE_REVOKED 44
#define Ztls_ALERT_CERTIFICATE_EXPIRED 45
#define Ztls_ALERT_CERTIFICATE_UNKNOWN 46
#define Ztls_ALERT_ILLEGAL_PARAMETER 47
#define Ztls_ALERT_UNKNOWN_CA 48
#define Ztls_ALERT_ACCESS_DENIED 49
#define Ztls_ALERT_DECODE_ERROR 50
#define Ztls_ALERT_DECRYPT_ERROR 51
#define Ztls_ALERT_PROTOCOL_VERSION 70
#define Ztls_ALERT_INTERNAL_ERROR 80
#define Ztls_ALERT_USER_CANCELED 90
#define Ztls_ALERT_MISSING_EXTENSION 109
#define Ztls_ALERT_UNSUPPORTED_EXTENSION 110
#define Ztls_ALERT_UNRECOGNIZED_NAME 112
#define Ztls_ALERT_UNKNOWN_PSK_IDENTITY 115
#define Ztls_ALERT_CERTIFICATE_REQUIRED 116
#define Ztls_ALERT_NO_APPLICATION_PROTOCOL 120
#define Ztls_ALERT_ECH_REQUIRED 121

/* internal errors */
#define Ztls_ERROR_NO_MEMORY (Ztls_ERROR_CLASS_INTERNAL + 1)
#define Ztls_ERROR_IN_PROGRESS (Ztls_ERROR_CLASS_INTERNAL + 2)
#define Ztls_ERROR_LIBRARY (Ztls_ERROR_CLASS_INTERNAL + 3)
#define Ztls_ERROR_INCOMPATIBLE_KEY (Ztls_ERROR_CLASS_INTERNAL + 4)
#define Ztls_ERROR_SESSION_NOT_FOUND (Ztls_ERROR_CLASS_INTERNAL + 5)
#define Ztls_ERROR_STATELESS_RETRY (Ztls_ERROR_CLASS_INTERNAL + 6)
#define Ztls_ERROR_NOT_AVAILABLE (Ztls_ERROR_CLASS_INTERNAL + 7)
#define Ztls_ERROR_COMPRESSION_FAILURE (Ztls_ERROR_CLASS_INTERNAL + 8)
#define Ztls_ERROR_REJECT_EARLY_DATA (Ztls_ERROR_CLASS_INTERNAL + 9)
#define Ztls_ERROR_DELEGATE (Ztls_ERROR_CLASS_INTERNAL + 10)
#define Ztls_ERROR_ASYNC_OPERATION (Ztls_ERROR_CLASS_INTERNAL + 11)
#define Ztls_ERROR_BLOCK_OVERFLOW (Ztls_ERROR_CLASS_INTERNAL + 12)

#define Ztls_ERROR_INCORRECT_PEM_KEY_VERSION (Ztls_ERROR_CLASS_INTERNAL + 60)
#define Ztls_ERROR_INCORRECT_PEM_ECDSA_KEY_VERSION (Ztls_ERROR_CLASS_INTERNAL + 61)
#define Ztls_ERROR_INCORRECT_PEM_ECDSA_CURVE (Ztls_ERROR_CLASS_INTERNAL + 62)
#define Ztls_ERROR_INCORRECT_PEM_ECDSA_KEYSIZE (Ztls_ERROR_CLASS_INTERNAL + 63)
#define Ztls_ERROR_INCORRECT_ASN1_ECDSA_KEY_SYNTAX (Ztls_ERROR_CLASS_INTERNAL + 64)

#define Ztls_HANDSHAKE_TYPE_CLIENT_HELLO 1
#define Ztls_HANDSHAKE_TYPE_SERVER_HELLO 2
#define Ztls_HANDSHAKE_TYPE_NEW_SESSION_TICKET 4
#define Ztls_HANDSHAKE_TYPE_END_OF_EARLY_DATA 5
#define Ztls_HANDSHAKE_TYPE_ENCRYPTED_EXTENSIONS 8
#define Ztls_HANDSHAKE_TYPE_CERTIFICATE 11
#define Ztls_HANDSHAKE_TYPE_CERTIFICATE_REQUEST 13
#define Ztls_HANDSHAKE_TYPE_CERTIFICATE_VERIFY 15
#define Ztls_HANDSHAKE_TYPE_FINISHED 20
#define Ztls_HANDSHAKE_TYPE_KEY_UPDATE 24
#define Ztls_HANDSHAKE_TYPE_COMPRESSED_CERTIFICATE 25
#define Ztls_HANDSHAKE_TYPE_MESSAGE_HASH 254
#define Ztls_HANDSHAKE_TYPE_PSEUDO_HRR -1

#define Ztls_CERTIFICATE_TYPE_X509 0
#define Ztls_CERTIFICATE_TYPE_RAW_PUBLIC_KEY 2

#define Ztls_ZERO_DIGEST_SHA256                                                                                                    \
    {0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,                               \
     0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55}

#define Ztls_ZERO_DIGEST_SHA384                                                                                                    \
    {0x38, 0xb0, 0x60, 0xa7, 0x51, 0xac, 0x96, 0x38, 0x4c, 0xd9, 0x32, 0x7e, 0xb1, 0xb1, 0xe3, 0x6a,                               \
     0x21, 0xfd, 0xb7, 0x11, 0x14, 0xbe, 0x07, 0x43, 0x4c, 0x0c, 0xc7, 0xbf, 0x63, 0xf6, 0xe1, 0xda,                               \
     0x27, 0x4e, 0xde, 0xbf, 0xe7, 0x6f, 0x65, 0xfb, 0xd5, 0x1a, 0xd2, 0xf1, 0x48, 0x98, 0xb9, 0x5b}

#define Ztls_ZERO_DIGEST_SHA512                                                                                                    \
    {0xcf, 0x83, 0xe1, 0x35, 0x7e, 0xef, 0xb8, 0xbd, 0xf1, 0x54, 0x28, 0x50, 0xd6, 0x6d, 0x80, 0x07,                               \
     0xd6, 0x20, 0xe4, 0x05, 0x0b, 0x57, 0x15, 0xdc, 0x83, 0xf4, 0xa9, 0x21, 0xd3, 0x6c, 0xe9, 0xce,                               \
     0x47, 0xd0, 0xd1, 0x3c, 0x5d, 0x85, 0xf2, 0xb0, 0xff, 0x83, 0x18, 0xd2, 0x87, 0x7e, 0xec, 0x2f,                               \
     0x63, 0xb9, 0x31, 0xbd, 0x47, 0x41, 0x7a, 0x81, 0xa5, 0x38, 0x32, 0x7a, 0xf9, 0x27, 0xda, 0x3e}

#define Ztls_TO__STR(n) #n
#define Ztls_TO_STR(n) Ztls_TO__STR(n)

/**
 * default maximum of tickets to send (see context_t::ticket_requests.server.max_count)
 */
#define Ztls_DEFAULT_MAX_TICKETS_TO_SERVE 4

typedef struct st_context_t context_t;
typedef struct st_key_schedule_t key_schedule_t;

/**
 * represents a sequence of octets
 */
using Bytes = ZuSpan<uint8_t>;

/**
 * used for storing output
 */
#define Ztls_HAVE_RECORD_CHUNKS 1

typedef struct st_buffer_t {
    uint8_t *base;
    size_t capacity;
    size_t off;
    uint8_t is_allocated; /* boolean */
    uint8_t align_bits;   /* if particular alignment is required, set to log2(alignment); otherwize zero */
    uint8_t tx;           /* direction: 0 for receive-side, 1 for transmit-side */
    Ztls::RecordSink *record_sink; /* internal synchronous record destination, otherwise NULL */
} buffer_t;

/**
 * key exchange context built by key_exchange_algorithm::create.
 */
typedef struct st_key_exchange_context_t {
    /**
     * the underlying algorithm
     */
    const struct st_key_exchange_algorithm_t *algo;
    /**
     * public key of this context
     */
    Bytes pubkey;
    /**
     * This function can be used for deriving a shared secret or for destroying the context.
     * When `secret` is non-NULL, this callback derives the shared secret using the private key of the context and the peer key
     * being given, and writes it to the caller-owned array. When `release` is set, the callee frees resources allocated to
     * the context and sets *keyex to NULL. On failure, an output stays unchanged or is cleared.
     */
    int (*on_exchange)(struct st_key_exchange_context_t **keyex, int release, KeyExchangeBytes *secret, Bytes peerkey);
} key_exchange_context_t;

/**
 * A key exchange algorithm.
 */
typedef const struct st_key_exchange_algorithm_t {
    /**
     * ID defined by the TLS specification
     */
    uint16_t id;
    /**
     * Creates a context for asynchronous key exchange. The function is called when ClientHello is generated. The on_exchange
     * callback of the created context is called when the client receives ServerHello.
     */
    int (*create)(const struct st_key_exchange_algorithm_t *algo, key_exchange_context_t **ctx);
    /**
     * Implements synchronous key exchange. Called when ServerHello is generated.
     * Given a public key provided by the peer (`peerkey`), this callback generates an ephemeral private and public key, and returns
     * the public key (`pubkey`) and a secret (`secret`) derived from the peerkey and private key.
     * On failure, the caller-owned outputs stay unchanged or are cleared.
     */
    int (*exchange)(const struct st_key_exchange_algorithm_t *algo, KeyExchangeBytes *pubkey, KeyExchangeBytes *secret,
                    Bytes peerkey);
    /**
     * crypto-specific data
     */
    intptr_t data;
    /**
     * Description as defined in the IANA TLS registry
     */
    const char *name;
} key_exchange_algorithm_t;

/**
 * context of a symmetric cipher
 */
typedef struct st_cipher_context_t {
    const struct st_cipher_algorithm_t *algo;
    /* field above this line must not be altered by the crypto binding */
    void (*do_dispose)(struct st_cipher_context_t *ctx);
    void (*do_init)(struct st_cipher_context_t *ctx, const void *iv);
    void (*do_transform)(struct st_cipher_context_t *ctx, void *output, const void *input, size_t len);
} cipher_context_t;

/**
 * a symmetric cipher
 */
typedef const struct st_cipher_algorithm_t {
    const char *name;
    size_t key_size;
    size_t block_size;
    size_t iv_size;
    size_t context_size;
    int (*setup_crypto)(cipher_context_t *ctx, int is_enc, const void *key);
} cipher_algorithm_t;

/**
 * This object specifies a 16-byte supplementary cipher operation calculated as part of AEAD encryption. QUIC and DTLS stacks can
 * use this object to derive a header-protection or packet-number-protection mask while protecting the packet payload.
 */
typedef struct st_aead_supplementary_encryption_t {
    /**
     * Cipher context to be used.
     */
    cipher_context_t *ctx;
    /**
     * Input to the supplementary cipher. This field may point into the AEAD output buffer, including ciphertext or tag bytes. AEAD
     * callbacks must read it only after the referenced output bytes have been written; `aead_encrypt_v_s` reads it after the
     * entire ciphertext-plus-tag output has been written.
     */
    const void *input;
    /**
     * Output.
     */
    uint8_t output[16];
} aead_supplementary_encryption_t;

/**
 * AEAD context.
 * AEAD implementations are allowed to stuff data at the end of the struct; see `aead_algorithm_t::setup_crypto`.
 * Encryption contexts MUST implement `do_encrypt`, `do_encrypt_v`, and `do_encrypt_v_s`; decryption contexts MUST implement
 * `do_decrypt`.
 * `do_encrypt_init`, `~update`, `~final` are obsolete, and therefore may not be available.
 */
typedef struct st_aead_context_t {
    /**
     * Points to the algorithm. This field is set by the TLS engine; backends must not alter.
     */
    const struct st_aead_algorithm_t *algo;
    /**
     * Mandatory callback that disposes of all the backend-specific data.
     */
    void (*dispose_crypto)(struct st_aead_context_t *ctx);
    /* Optional checked serial encryption. A chunk writes exactly its input length; end writes only the tag. */
    int (*do_enc_begin)(struct st_aead_context_t *ctx, uint64_t seq, const void *aad, size_t aadlen);
    int (*do_enc_chunk)(struct st_aead_context_t *ctx, void *output, const void *input, size_t length, size_t *written);
    int (*do_enc_end)(struct st_aead_context_t *ctx, void *output, size_t capacity, size_t *written);

    /* Checked serial decryption: output is provisional until do_dec_end authenticates the tag.
     * Supported providers write exactly length bytes per chunk; no unbounded provider buffering. */
    int (*do_dec_begin)(struct st_aead_context_t *ctx, uint64_t seq, const void *aad, size_t aadlen);
    int (*do_dec_chunk)(struct st_aead_context_t *ctx, void *output, size_t capacity,
                        const void *input, size_t length, size_t *written);
    int (*do_dec_end)(struct st_aead_context_t *ctx, const void *tag, size_t taglen);
    void (*do_dec_abort)(struct st_aead_context_t *ctx);
    /**
     * Mandatory callback that returns the static IV. The size of IV is available as `aead_algorithm_t::iv_size`.
     */
    void (*do_get_iv)(struct st_aead_context_t *ctx, void *iv);
    /**
     * Mandatory callback that sets the static IV. The size of IV is available as `aead_algorithm_t::iv_size`.
     */
    void (*do_set_iv)(struct st_aead_context_t *ctx, const void *iv);
    /**
     * Deprecated.
     */
    void (*do_encrypt_init)(struct st_aead_context_t *ctx, uint64_t seq, const void *aad, size_t aadlen);
    /**
     * Deprecated.
     */
    size_t (*do_encrypt_update)(struct st_aead_context_t *ctx, void *output, const void *input, size_t inlen);
    /**
     * Deprecated.
     */
    size_t (*do_encrypt_final)(struct st_aead_context_t *ctx, void *output);
    /**
     * Mandatory callback that does "one-shot" encryption of an AEAD block.
     * When `supp` is set to non-NULL, the callback must also encrypt the supplementary block.
     * Backends may set this field to `aead__do_encrypt` that calls `do_encrypt_v` and `cipher_*` functions for handling
     * the supplementary block.
     */
    void (*do_encrypt)(struct st_aead_context_t *ctx, void *output, const void *input, size_t inlen, uint64_t seq,
                       const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp);
    /**
     * Variant of `do_encrypt` that gathers read-only input from multiple blocks. Support for this callback is also mandatory.
     * Legacy backends may set this field to `aead__do_encrypt_v` that calls `do_encrypt_init`, `do_encrypt_update`,
     * `do_encrypt_final`.
     */
    void (*do_encrypt_v)(struct st_aead_context_t *ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq,
                         const void *aad, size_t aadlen);
    /**
     * Variant of `do_encrypt` that gathers read-only plaintext from multiple vectors and, when `supp` is non-NULL, computes the
     * supplementary block after all AEAD output has been written. Plaintext vector ranges are expected not to overlap `output`;
     * `supp->input` may point into `output`.
     */
    void (*do_encrypt_v_s)(struct st_aead_context_t *ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq,
                           const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp);
    /**
     * Mandatory callback for decrypting an AEAD block.
     * If successful, returns the amount of cleartext bytes being written to output. Otherwise, returns SIZE_MAX.
     */
    size_t (*do_decrypt)(struct st_aead_context_t *ctx, void *output, const void *input, size_t inlen, uint64_t seq,
                         const void *aad, size_t aadlen);
} aead_context_t;

/**
 * An AEAD cipher.
 */
typedef const struct st_aead_algorithm_t {
    /**
     * name (following the convention of `openssl ciphers -v ALL`)
     */
    const char *name;
    /**
     * confidentiality_limit (max records / packets sent before re-key)
     */
    const uint64_t confidentiality_limit;
    /**
     * integrity_limit (max decryption failure records / packets before re-key)
     */
    const uint64_t integrity_limit;
    /**
     * the underlying key stream
     */
    cipher_algorithm_t *ctr_cipher;
    /**
     * the underlying ecb cipher (might not be available)
     */
    cipher_algorithm_t *ecb_cipher;
    /**
     * key size
     */
    size_t key_size;
    /**
     * size of the IV
     */
    size_t iv_size;
    /**
     * size of the tag
     */
    size_t tag_size;
        /**
     * if encrypted bytes are going to be written using non-temporal store instructions (i.e., skip cache)
     */
    unsigned non_temporal : 1;
    /**
     * log2(alignment) being required
     */
    uint8_t align_bits;
    /**
     * size of memory allocated for `aead_context_t`
     */
    size_t context_size;
    /**
     * Backend callback called to setup `aead_context_t`.
     * Backends are allowed to stuff arbitrary data at the end of `aead_context_t`; actual size of the memory chunk being
     * allocated is that specified by `aead_algorithm_t::context_size`. When the `setup_crypto` callback is called, all the
     * fields outside of `aead_context_t` will be in undefined state; it is the responsibility of the callback to initialize
     * them, as well as the callbacks of `aead_context_t` that the backend supports.
     * A non-zero return value indicates failure, in which case the error will propagate as `aead_new` returning NULL.
     */
    int (*setup_crypto)(aead_context_t *ctx, int is_enc, const void *key, const void *iv);
    /** Serial callback capabilities. Providers advertising a capability must install its complete callback set.
     * Wrappers that remove callbacks must clear the corresponding capability. */
    unsigned serial_encrypt : 1;
    unsigned serial_decrypt : 1;
} aead_algorithm_t;

/**
 *
 */
typedef enum en_hash_final_mode_t {
    /**
     * obtains the digest and frees the context
     */
    HASH_FINAL_MODE_FREE = 0,
    /**
     * obtains the digest and reset the context to initial state
     */
    HASH_FINAL_MODE_RESET = 1,
    /**
     * obtains the digest while leaving the context as-is
     */
    HASH_FINAL_MODE_SNAPSHOT = 2
} hash_final_mode_t;

/**
 * A hash context.
 */
typedef struct st_hash_context_t {
    /**
     * feeds additional data into the hash context
     */
    void (*update)(struct st_hash_context_t *ctx, const void *src, size_t len);
    /**
     * returns the digest and performs necessary operation specified by mode
     */
    void (*final)(struct st_hash_context_t *ctx, void *md, hash_final_mode_t mode);
    /**
     * creates a copy of the hash context
     */
    struct st_hash_context_t *(*clone_)(struct st_hash_context_t *src);
} hash_context_t;

/**
 * A hash algorithm and its properties.
 */
typedef const struct st_hash_algorithm_t {
    /**
     * name of the hash algorithm
     */
    const char *name;
    /**
     * block size
     */
    size_t block_size;
    /**
     * digest size
     */
    size_t digest_size;
    /**
     * constructor that creates the hash context
     */
    hash_context_t *(*create)(void);
    /**
     * digest of zero-length octets
     */
    uint8_t empty_digest[Ztls_MAX_DIGEST_SIZE];
} hash_algorithm_t;

typedef const struct st_cipher_suite_t {
    /**
     * ID as defined by the TLS Cipher Suites registry
     */
    uint16_t id;
    /**
     * underlying AEAD algorithm
     */
    aead_algorithm_t *aead;
    /**
     * underlying hash algorithm
     */
    hash_algorithm_t *hash;
    /**
     * value of the "Description" field of the TLS Cipher Suites registry
     */
    const char *name;
} cipher_suite_t;

struct TrafficProtection;

typedef struct st_message_emitter_t {
    buffer_t *buf;
    struct TrafficProtection *enc;
    size_t record_header_length;
    int (*begin_message)(struct st_message_emitter_t *self);
    int (*commit_message)(struct st_message_emitter_t *self);
} message_emitter_t;

/**
 * HPKE KEM
 */
typedef const struct st_hpke_kem_t {
    uint16_t id;
    key_exchange_algorithm_t *keyex;
    hash_algorithm_t *hash;
} hpke_kem_t;

typedef struct st_hpke_cipher_suite_id_t {
    uint16_t kdf;
    uint16_t aead;
} hpke_cipher_suite_id_t;

typedef const struct st_hpke_cipher_suite_t {
    hpke_cipher_suite_id_t id;
    const char *name; /* in form of "<kdf>/<aead>" using the sames specified in IANA HPKE registry */
    hash_algorithm_t *hash;
    aead_algorithm_t *aead;
} hpke_cipher_suite_t;

#define Ztls_CALLBACK_TYPE0(ret, name)                                                                                             \
    typedef struct st_##name##_t {                                                                                            \
        ret (*cb)(struct st_##name##_t * self);                                                                               \
    } name##_t

#define Ztls_CALLBACK_TYPE(ret, name, ...)                                                                                         \
    typedef struct st_##name##_t {                                                                                            \
        ret (*cb)(struct st_##name##_t * self, __VA_ARGS__);                                                                  \
    } name##_t

typedef struct st_client_hello_psk_identity_t {
    Bytes identity;
    uint32_t obfuscated_ticket_age;
    Bytes binder;
} client_hello_psk_identity_t;

/**
 * arguments passsed to the on_client_hello callback
 */
typedef struct st_on_client_hello_parameters_t {
    /**
     * SNI value received from the client. The value is {NULL, 0} if the extension was absent.
     */
    Bytes server_name;
    /**
     * Raw value of the client_hello message.
     */
    Bytes raw_message;
    /**
     * points to the cipher-suites section of the raw_message (see above)
     */
    Bytes cipher_suites;
    /**
     *
     */
    struct {
        Bytes *list;
        size_t count;
    } negotiated_protocols;
    struct {
        const uint16_t *list;
        size_t count;
    } signature_algorithms;
    struct {
        const uint16_t *list;
        size_t count;
    } certificate_compression_algorithms;
    struct {
        const uint8_t *list;
        size_t count;
    } server_certificate_types;
    struct {
        const client_hello_psk_identity_t *list;
        size_t count;
    } psk_identities;
    /**
     * set to 1 if ClientHello is too old (or too new) to be handled by this TLS implementation
     */
    unsigned incompatible_version : 1;
} on_client_hello_parameters_t;

/**
 * returns current time in milliseconds (get_time can be used to return the physical time)
 */
Ztls_CALLBACK_TYPE0(uint64_t, get_time);
/**
 * after receiving ClientHello, the core calls the optional callback to give a chance to the swap the context depending on the input
 * values. The callback is required to call `set_server_name` if an SNI extension needs to be sent to the client.
 */
Ztls_CALLBACK_TYPE(int, on_client_hello, Ztls::Cxn *tls, on_client_hello_parameters_t *params);
/**
 * callback to generate the certificate message. `context::certificates` are set when the callback is set to NULL.
 */
Ztls_CALLBACK_TYPE(int, emit_certificate, Ztls::Cxn *tls, message_emitter_t *emitter, key_schedule_t *key_sched,
                   Bytes context, int push_status_request, const uint16_t *compress_algos, size_t num_compress_algos);
/**
 * Sign CertificateVerify. A server signer may suspend by installing a
 * completion and cleanup function on the connection and returning
 * Ztls_ERROR_ASYNC_OPERATION. The callbacks must not retain the connection.
 */
Ztls_CALLBACK_TYPE(int, sign_certificate, Ztls::Cxn *tls, bool allow_async, uint16_t *selected_algorithm,
                   buffer_t *output, Bytes input, const uint16_t *algorithms, size_t num_algorithms);
/**
 * after receiving Certificate, the core calls the callback to verify the certificate chain and to obtain a pointer to a
 * callback that should be used for verifying CertificateVerify. If an error occurs between a successful return from this
 * callback to the invocation of the verify_sign callback, verify_sign is called with both data and sign set to an empty buffer.
 * The implementor of the callback should use that as the opportunity to free any temporary data allocated for the verify_sign
 * callback.
 * The name of the server to be verified, if any, is provided explicitly as `server_name`. When ECH is offered by the client but
 * the was rejected by the server, this value can be different from that being sent via `get_server_name`.
 */
typedef struct st_verify_certificate_t {
    int (*cb)(struct st_verify_certificate_t *self, Ztls::Cxn *tls, const char *server_name,
              int (**verify_sign)(void *verify_ctx, uint16_t algo, Bytes data, Bytes sign), void **verify_data,
              Bytes *certs, size_t num_certs);
    /**
     * list of signature algorithms being supported, terminated by UINT16_MAX
     */
    const uint16_t *algos;
} verify_certificate_t;
/**
 * Encrypt-and-signs (or verify-and-decrypts) a ticket (server-only).
 * When used for encryption (i.e., is_encrypt being set), the function should return 0 if successful, or else a non-zero value.
 * When used for decryption, the function should return 0 (successful), Ztls_ERROR_REJECT_EARLY_DATA (successful, but 0-RTT is
 * forbidden), or any other value to indicate failure.
 */
Ztls_CALLBACK_TYPE(int, encrypt_ticket, Ztls::Cxn *tls, int is_encrypt, buffer_t *dst, Bytes src);
/**
 * saves a ticket (client-only)
 */
Ztls_CALLBACK_TYPE(int, save_ticket, Ztls::Cxn *tls, Bytes input);
/**
 * event logging (incl. secret logging)
 */
/**
 * reference counting
 */
/**
 * applications that have their own record layer can set this function to derive their own traffic keys from the traffic secret.
 * The cipher-suite that is being associated to the connection can be obtained by calling the get_cipher function.
 */
Ztls_CALLBACK_TYPE(int, update_traffic_key, Ztls::Cxn *tls, int is_enc, size_t epoch, const void *secret);
/**
 * callback for every extension detected during decoding
 */
Ztls_CALLBACK_TYPE(int, on_extension, Ztls::Cxn *tls, uint8_t hstype, uint16_t exttype, Bytes extdata);
/**
 *
 */
typedef struct st_decompress_certificate_t {
    /**
     * list of supported algorithms terminated by UINT16_MAX
     */
    const uint16_t *supported_algorithms;
    /**
     * callback that decompresses the message
     */
    int (*cb)(struct st_decompress_certificate_t *self, Ztls::Cxn *tls, uint16_t algorithm, Bytes output,
              Bytes input);
} decompress_certificate_t;
/**
 * ECH: creates the AEAD context to be used for "Open"-ing inner CH. Given `config_id`, the callback looks up the ECH config and the
 * corresponding private key, invokes `hpke_setup_base_r` with provided `cipher`, `enc`, and `info_prefix` (which will be
 * "tls ech" || 00).
 */
Ztls_CALLBACK_TYPE(aead_context_t *, ech_create_opener, hpke_kem_t **kem, hpke_cipher_suite_t **cipher, Ztls::Cxn *tls,
                   uint8_t config_id, hpke_cipher_suite_id_t cipher_id, Bytes enc, Bytes info_prefix);

/**
 * the configuration
 */
struct st_context_t {
    /**
     * PRNG to be used
     */
    void (*random_bytes)(void *buf, size_t len);
    /**
     *
     */
    get_time_t *get_time;
    /**
     * list of supported key-exchange algorithms terminated by NULL
     */
    key_exchange_algorithm_t **key_exchanges;
    /**
     * list of supported cipher-suites terminated by NULL
     */
    cipher_suite_t **cipher_suites;
    /**
     * list of certificates
     */
    CertificateChain certificates;
    /**
     * External pre-shared key used for mutual authentication. Unless when using PSK, all the fields must be set to NULL / 0.
     */
    struct {
        Bytes identity;
        Bytes secret;
        /**
         * (mandatory) hash algorithm associated to the PSK; cipher-suites not sharing the same `hash_algorithm_t` will be
         * ignored
         */
        hash_algorithm_t *hash;
    } pre_shared_key;
    /**
     * ECH
     */
    struct {
        struct {
            /**
             * list of HPKE symmetric cipher-suites (set to NULL to disable ECH altogether)
             */
            hpke_cipher_suite_t **ciphers;
            /**
             * KEMs being supported
             */
            hpke_kem_t **kems;
        } client;
        struct {
            /**
             * callback that does ECDH key exchange and returns the AEAD context
             */
            ech_create_opener_t *create_opener;
            /**
             * ECHConfigList to be sent to the client when there is mismatch (or when the client sends a grease)
             */
            Bytes retry_configs;
        } server;
    } ech;
    /**
     *
     */
    on_client_hello_t *on_client_hello;
    /**
     *
     */
    emit_certificate_t *emit_certificate;
    /**
     *
     */
    sign_certificate_t *sign_certificate;
    /**
     *
     */
    verify_certificate_t *verify_certificate;
    /**
     * lifetime of a session ticket (server-only)
     */
    uint32_t ticket_lifetime;
    /**
     * maximum permitted size of early data (server-only)
     */
    uint32_t max_early_data_size;
    /**
     * maximum size of the message buffer (default: 0 = unlimited = 3 + 2^24 bytes)
     */
    size_t max_buffer_size;
    /**
     * this field is obsolete and ignored
     */
    const char *hkdf_label_prefix__obsolete;
    /**
     * if set, psk handshakes use (ec)dhe
     */
    unsigned require_dhe_on_psk : 1;
    /**
     * if exporter master secrets should be recorded
     */
    unsigned use_exporter : 1;
    /**
     * if ChangeCipherSpec record should be sent during handshake. If the client sends CCS, the server sends one in response
     * regardless of the value of this flag. See RFC 8446 Appendix D.3.
     */
    unsigned send_change_cipher_spec : 1;
    /**
     * if set, the server requests client certificates to authenticate the client
     */
    unsigned require_client_authentication : 1;
    /**
     * if set, EOED will not be emitted or accepted
     */
    unsigned omit_end_of_early_data : 1;
    /**
     * This option turns on support for Raw Public Keys (RFC 7250).
     *
     * When running as a client, this option instructs the client to request the server to send raw public keys in place of X.509
     * certificate chain. The client should set its `certificate_verify` callback to one that is capable of validating the raw
     * public key that will be sent by the server.
     *
     * When running as a server, this option instructs the server to only handle clients requesting the use of raw public keys. If
     * the client does not, the handshake is rejected. Note however that the rejection happens only after the `on_client_hello`
     * callback is being called. Therefore, applications can support both X.509 and raw public keys by swapping `context_t` to
     * the correct one when that callback is being called (like handling swapping the contexts based on the value of SNI).
     */
    unsigned use_raw_public_keys : 1;
    /**
     * boolean indicating if the cipher-suite should be chosen based on server's preference
     */
    unsigned server_cipher_preference : 1;
    /**
     * boolean indicating if ChaCha20-Poly1305 should be reprioritized to the top of the server cipher list if a ChaCha20-Poly1305
     * cipher is at the top of the client cipher list
     */
    unsigned server_cipher_chacha_priority : 1;
    /**
     *
     */
    encrypt_ticket_t *encrypt_ticket;
    /**
     *
     */
    save_ticket_t *save_ticket;
    /**
     *
     */
        /**
     *
     */
    update_traffic_key_t *update_traffic_key;
    /**
     *
     */
    decompress_certificate_t *decompress_certificate;
    /**
     *
     */
    on_extension_t *on_extension;
        /**
     * (optional) session ID Context to segment resumption
     */
    struct {
        uint8_t bytes[Ztls_SHA256_DIGEST_SIZE];
        unsigned is_set : 1;
    } ticket_context;
    /**
     * (optional) list of CAs advertised to clients as supported in the CertificateRequest message; each item must be DNs in DER
     * format. The values are sent to the client only when `context_t::require_client_authentication` is set to true.
     */
    struct {
        const Bytes *list;
        size_t count;
    } client_ca_names;
    /**
     * (optional)
     */
    struct {
        /**
         * if set to non-zero and if the save_ticket callback is provided, a ticket_request extension containing the specified
         * values is sent
         */
        struct {
            uint8_t new_session_count;
            uint8_t resumption_count;
        } client;
        /**
         * if set to non-zero, the maximum number of tickets being sent is capped to the specifed value; if set to zero, the maximum
         * adopted is Ztls_DEFAULT_MAX_TICKETS_TO_SERVE.
         */
        struct {
            uint8_t max_count;
        } server;
    } ticket_requests;
};

typedef struct st_raw_extension_t {
    uint16_t type;
    Bytes data;
} raw_extension_t;

typedef enum en_early_data_acceptance_t {
    EARLY_DATA_ACCEPTANCE_UNKNOWN = 0,
    EARLY_DATA_REJECTED,
    EARLY_DATA_ACCEPTED
} early_data_acceptance_t;

/**
 * optional arguments to client-driven handshake
 */
#ifdef _MSC_VER
/* suppress warning C4201: nonstandard extension used: nameless struct/union */
#pragma warning(push)
#pragma warning(disable : 4201)
#endif
typedef struct st_handshake_properties_t {
    struct {
        /**
         * list of protocols offered through ALPN
         */
        struct {
            const Bytes *list;
            size_t count;
        } negotiated_protocols;
        /**
         * session ticket sent to the application via save_ticket callback
         */
        Bytes session_ticket;
        /**
         * pointer to store the maximum size of early-data that can be sent immediately. If set to non-NULL, the first call to
         * handshake (or handle_message) will set `*max_early_data` to the value obtained from the session ticket, or
         * to zero if early-data cannot be sent. If NULL, early data will not be used.
         */
        size_t *max_early_data_size;
        /**
         * If early-data has been accepted by peer, or if the state is still unknown. The state changes anytime after handshake
         * keys become available. Applications can peek the tri-state variable every time it calls `hanshake` or
         * `handle_message` to determine the result at the earliest moment. This is an output parameter.
         */
        early_data_acceptance_t early_data_acceptance;
        /**
         * negotiate the key exchange method before sending key_share
         */
        unsigned negotiate_before_key_exchange : 1;
        /**
         * ECH
         */
        struct {
            /**
             * An ECH config offered by server e.g., by HTTPS RR. An empty config with a non-null data pointer, a grease ECH will
             * be sent, assuming that X25519-SHA256 KEM and SHA256-AES-128-GCM HPKE cipher is available. If .base is also NULL,
             * ECH will not be used at all, even if the context provided the ECH ciphers.
             */
            Bytes configs;
            /**
             * slot to save the config obtained from server on mismatch; user must free the returned blob by calling `free`
             */
            EchBytes *retry_configs;
        } ech;
    } client;
    struct {
        /**
         * psk binder being selected (len is set to zero if none)
         */
        struct {
            uint8_t base[Ztls_MAX_DIGEST_SIZE];
            size_t len;
        } selected_psk_binder;
        /**
         * parameters related to use of the Cookie extension
         */
        struct {
            /**
             * HMAC key to protect the integrity of the cookie. The key should be as long as the digest size of the first
             * ciphersuite specified in context_t (i.e. the hash algorithm of the best ciphersuite that can be chosen).
             */
            const void *key;
            /**
             * additional data to be used for verifying the cookie
             */
            Bytes additional_data;
        } cookie;
        /**
         * if HRR should always be sent
         */
        unsigned enforce_retry : 1;
        /**
         * if retry should be stateless (cookie.key MUST be set when this option is used)
         */
        unsigned retry_uses_cookie : 1;
    } server;
    /**
     * an optional list of additional extensions to send either in CH or EE, terminated by type == UINT16_MAX
     */
    raw_extension_t *additional_extensions;
    /**
     * an optional callback that returns a boolean value indicating if a particular extension should be collected
     */
    int (*collect_extension)(Ztls::Cxn *tls, struct st_handshake_properties_t *properties, uint16_t type);
    /**
     * an optional callback that reports the extensions being collected
     */
    int (*collected_extensions)(Ztls::Cxn *tls, struct st_handshake_properties_t *properties, raw_extension_t *extensions);
} handshake_properties_t;
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#ifdef _MSC_VER
/* suppress warning C4293: >> shift count negative or too big */
#pragma warning(disable : 4293)
#endif
/**
 * builds a new Bytes instance using the supplied parameters
 */
static Bytes wire_bytes(const void *p, size_t len);
/**
 * initializes a buffer, setting the default destination to the small buffer provided as the argument.
 * `tx` is direction (0: receive-side, 1: transmit-side)
 */
static void buffer_init(buffer_t *buf, void *smallbuf, size_t smallbuf_size, int tx);
/**
 * initializes a receive-side buffer
 */
static void buffer_init_rx(buffer_t *buf, void *smallbuf, size_t smallbuf_size);
/**
 * initializes a transmit-side buffer
 */
static void buffer_init_tx(buffer_t *buf, void *smallbuf, size_t smallbuf_size);
/**
 * disposes a buffer, freeing resources allocated by the buffer itself (if any)
 */
static void buffer_dispose(buffer_t *buf);
/**
 * internal
 */
ZtlsAPI void buffer__release_memory(buffer_t *buf);
/**
 * reserves space for additional amount of memory
 */
ZtlsAPI int buffer_reserve(buffer_t *buf, size_t delta, int tx);
/**
 * reserves space for additional amount of memory, requiring `buf->base` to follow specified alignment
 */
int buffer_reserve_aligned(buffer_t *buf, size_t delta, uint8_t align_bits, int tx);
/**
 * internal
 */
int buffer__do_pushv(buffer_t *buf, const void *src, size_t len);
#define Ztls_buffer_pushv(buf, src, len)                                                                                           \
    do {                                                                                                                           \
        if ((ret = buffer__do_pushv((buf), (src), (len))) != 0)                                                               \
            goto Exit;                                                                                                             \
    } while (0)

#define Ztls_buffer_push(buf, ...)                                                                                                 \
    do {                                                                                                                           \
        if ((ret = buffer__do_pushv((buf), (uint8_t[]){__VA_ARGS__}, sizeof((uint8_t[]){__VA_ARGS__}))) != 0)                 \
            goto Exit;                                                                                                             \
    } while (0)

#define Ztls_buffer_push16(buf, v) \
    do { \
        auto _v = ZuBE(uint16_t(v)); \
        Ztls_buffer_pushv((buf), &_v, sizeof(_v)); \
    } while (0)

#define Ztls_buffer_push24(buf, v)                                                                                                 \
    do {                                                                                                                           \
        uint32_t _v = (v);                                                                                                         \
        Ztls_buffer_push(buf, (uint8_t)(_v >> 16), (uint8_t)(_v >> 8), (uint8_t)_v);                                               \
    } while (0)

#define Ztls_buffer_push32(buf, v) \
    do { \
        auto _v = ZuBE(uint32_t(v)); \
        Ztls_buffer_pushv((buf), &_v, sizeof(_v)); \
    } while (0)

#define Ztls_buffer_push64(buf, v) \
    do { \
        auto _v = ZuBE(uint64_t(v)); \
        Ztls_buffer_pushv((buf), &_v, sizeof(_v)); \
    } while (0)

#define Ztls_buffer_push_block(buf, _capacity, block) \
    do { \
        size_t capacity = (_capacity); \
        Ztls_buffer_pushv((buf), "\0\0\0\0\0\0\0", capacity); \
        size_t body_start = (buf)->off; \
        do { \
            block \
        } while (0); \
        size_t body_size = (buf)->off - body_start; \
        if (capacity < sizeof(size_t) && body_size >= (size_t)1 << (capacity * 8)) { \
            ret = Ztls_ERROR_BLOCK_OVERFLOW; \
            goto Exit; \
        } \
        for (; capacity != 0; --capacity) \
            (buf)->base[body_start - capacity] = (uint8_t)(body_size >> (8 * (capacity - 1))); \
    } while (0)

#define Ztls_buffer_push_message_body(buf, key_sched, type, block)                                                                 \
    do {                                                                                                                           \
        buffer_t *_buf = (buf);                                                                                               \
        key_schedule_t *_key_sched = (key_sched);                                                                             \
        size_t mess_start = _buf->off;                                                                                             \
        Ztls_buffer_push(_buf, (type));                                                                                            \
        Ztls_buffer_push_block(_buf, 3, block);                                                                                    \
        if (_key_sched != NULL)                                                                                                    \
            _key_schedule_update_hash(_key_sched, _buf->base + mess_start, _buf->off - mess_start, 0);                        \
    } while (0)

#define Ztls_push_message(emitter, key_sched, type, block)                                                                         \
    do {                                                                                                                           \
        message_emitter_t *_emitter = (emitter);                                                                              \
        if ((ret = _emitter->begin_message(_emitter)) != 0)                                                                        \
            goto Exit;                                                                                                             \
        Ztls_buffer_push_message_body(_emitter->buf, (key_sched), (type), block);                                                  \
        if ((ret = _emitter->commit_message(_emitter)) != 0)                                                                       \
            goto Exit;                                                                                                             \
    } while (0)

int decode8(uint8_t *value, const uint8_t **src, const uint8_t *end);
int decode16(uint16_t *value, const uint8_t **src, const uint8_t *end);
int decode24(uint32_t *value, const uint8_t **src, const uint8_t *end);
int decode32(uint32_t *value, const uint8_t **src, const uint8_t *end);
int decode64(uint64_t *value, const uint8_t **src, const uint8_t *end);

#define Ztls_decode_open_block(src, end, capacity, block) \
    do { \
        size_t _capacity = (capacity); \
        if (_capacity > (size_t)(end - (src))) { \
            ret = Ztls_ALERT_DECODE_ERROR; \
            goto Exit; \
        } \
        size_t _block_size = 0; \
        do { \
            _block_size = _block_size << 8 | *(src)++; \
        } while (--_capacity != 0); \
        if (_block_size > (size_t)(end - (src))) {                                                                                 \
            ret = Ztls_ALERT_DECODE_ERROR;                                                                                         \
            goto Exit;                                                                                                             \
        }                                                                                                                          \
        do {                                                                                                                       \
            const uint8_t *const end = (src) + _block_size;                                                                        \
            do {                                                                                                                   \
                block                                                                                                              \
            } while (0);                                                                                                           \
            if ((src) != end) {                                                                                                    \
                ret = Ztls_ALERT_DECODE_ERROR;                                                                                     \
                goto Exit;                                                                                                         \
            }                                                                                                                      \
        } while (0);                                                                                                               \
    } while (0)

#define Ztls_decode_assert_block_close(src, end)                                                                                   \
    do {                                                                                                                           \
        if ((src) != end) {                                                                                                        \
            ret = Ztls_ALERT_DECODE_ERROR;                                                                                         \
            goto Exit;                                                                                                             \
        }                                                                                                                          \
    } while (0);

#define Ztls_decode_block(src, end, capacity, block)                                                                               \
    do {                                                                                                                           \
        Ztls_decode_open_block((src), end, capacity, block);                                                                       \
        Ztls_decode_assert_block_close((src), end);                                                                                \
    } while (0)

/**
 * returns address of the crypto callbacks that the connection is using
 */
context_t *get_context(Ztls::Cxn *tls);
/**
 * updates the context of a connection. Can be called from `on_client_hello` callback.
 */
void set_context(Ztls::Cxn *tls, context_t *ctx);
/**
 * returns the client-random
 */
Bytes get_client_random(Ztls::Cxn *tls);
/**
 * returns the cipher-suite being used
 */
ZtlsAPI cipher_suite_t *get_cipher(Ztls::Cxn *tls);
/**
 * returns a supported cipher-suite given an id
 */
ZtlsAPI cipher_suite_t *find_cipher_suite(cipher_suite_t **cipher_suites, uint16_t id);
/**
 * Returns the negotiated TLS 1.3 protocol identifier, 0x0304.
 */
ZtlsAPI uint16_t get_protocol_version(Ztls::Cxn *tls);
/**
 * Returns current state of traffic keys. The cipher-suite being used, as well as the length of the traffic keys, can be obtained
 * via `get_cipher`.
 * TODO: Even in case of offloading just the TX side, there should be API for handling key updates, sending Close aleart.
 */
int get_traffic_keys(Ztls::Cxn *tls, int is_enc, uint8_t *key, uint8_t *iv, uint64_t *seq);
/**
 * returns the server-name (NULL if SNI is not used or failed to negotiate)
 */
const char *get_server_name(Ztls::Cxn *tls);
/**
 * sets the server-name associated to the TLS connection. If server_name_len is zero, then strlen(server_name) is called to
 * determine the length of the name.
 * On the client-side, the value is used for certificate validation. The value will be also sent as an SNI extension, if it looks
 * like a DNS name.
 * On the server-side, it can be called from on_client_hello to indicate the acceptance of the SNI extension to the client.
 */
ZtlsAPI int set_server_name(Ztls::Cxn *tls, const char *server_name, size_t server_name_len);
/**
 * returns the negotiated protocol (or NULL)
 */
ZtlsAPI const char *get_negotiated_protocol(Ztls::Cxn *tls);
/**
 * sets the negotiated protocol. If protocol_len is zero, strlen(protocol) is called to determine the length of the protocol name.
 */
ZtlsAPI int set_negotiated_protocol(Ztls::Cxn *tls, const char *protocol, size_t protocol_len);
/**
 * returns if the handshake has been completed
 */
ZtlsAPI int handshake_is_complete(Ztls::Cxn *tls);
/**
 * returns if a PSK (or PSK-DHE) handshake was performed
 */
ZtlsAPI int is_psk_handshake(Ztls::Cxn *tls);
/**
 * return if a ECH handshake was performed, as well as optionally the kem and cipher-suite being used
 * FIXME: this function always return false when the TLS session is exported and imported
 */
int is_ech_handshake(Ztls::Cxn *tls, uint8_t *config_id, hpke_kem_t **kem, hpke_cipher_suite_t **cipher);
/**
 * returns a pointer to user data pointer (client is reponsible for freeing the associated data prior to calling free)
 */
ZtlsAPI void **get_data_ptr(Ztls::Cxn *tls);
/**
 * proceeds with the handshake, optionally taking some input from peer. The function returns zero in case the handshake completed
 * successfully. Ztls_ERROR_IN_PROGRESS is returned in case the handshake is incomplete. Otherwise, an error value is returned. The
 * contents of sendbuf should be sent to the client, regardless of whether if an error is returned. inlen is an argument used for
 * both input and output. As an input, the arguments takes the size of the data available as input. Upon return the value is updated
 * to the number of bytes consumed by the handshake. In case the returned value is Ztls_ERROR_IN_PROGRESS there is a guarantee that
 * all the input are consumed (i.e. the value of inlen does not change).
 */
ZtlsAPI int handshake(Ztls::Cxn *tls, buffer_t *sendbuf, const void *input, size_t *inlen, handshake_properties_t *args);
/**
 * decrypts the first record within given buffer
 */
ZtlsAPI int receive(Ztls::Cxn *tls, buffer_t *plaintextbuf, const void *input, size_t *len);
/**
 * encrypts given buffer into multiple TLS records
 */
ZtlsAPI int send(Ztls::Cxn *tls, buffer_t *sendbuf, const void *input, size_t inlen);
/**
 * updates the send traffic key (as well as asks the peer to update)
 */
int update_key(Ztls::Cxn *tls, int request_update);
/** Mark the point where the Tx thread assumes ownership of the outbound members. */
ZtlsAPI int handoff_tx(Ztls::Cxn *tls);
/**
 * encrypts application data with the connection's outbound record protection
 */
/* All chunks output APIs: reserve failure leaves outbound state retryable. After a successful
 * reserve, any emission or finish failure makes outbound state terminal: subsequent chunks and
 * contiguous sends fail with Ztls_ERROR_LIBRARY. Discard must retract the entire reservation;
 * commit only advances private output, and finish publishes it. Never retry a terminal Tx.
 * Handshake/receive callers must tear down the connection on errors other than their documented
 * IN_PROGRESS / ASYNC_OPERATION continuations; prior records may already have been published. */
ZtlsAPI int tx_send_chunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, const void *input, size_t length);
ZtlsAPI int tx_key_chunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, int request_update);
ZtlsAPI int tx_alert_chunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, uint8_t level, uint8_t description);
int handshake_chunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, const void *input, size_t *length,
                          handshake_properties_t *properties);

/** Maximum private record output: the supported encrypted record bound minus its tag.
 * Reserve the full encrypted bound to cover every supported tag size and TLS 1.3 inner padding.
 * The application-visible extent is always limited to Ztls_RECEIVE_APPLICATION_CAPACITY. */
#define Ztls_HAVE_RECEIVE_STREAM 1
#define Ztls_RECEIVE_APPLICATION_CAPACITY 16384
#define Ztls_RECEIVE_RECORD_CAPACITY (16384 + 256)
/* Receive one record incrementally, retaining provisional plaintext privately
 * across fragments until authentication. The allocator remains live through
 * commit or discard. The input length reports consumed bytes on return. */
ZtlsAPI int receive_stream(Ztls::Cxn *tls, Ztls::RecordSink *output,
                           const void *input, size_t *length, handshake_properties_t *properties);
ZtlsAPI int receive_stream_check(Ztls::Cxn *tls);

/** Return overhead for the connection's negotiated Tx record protection. */
ZtlsAPI size_t tx_get_record_overhead(Ztls::Cxn *tls);
/**
 * consumes one pending peer request for a reciprocal KeyUpdate; returns one when consumed, otherwise zero
 */
ZtlsAPI int take_key_update_request(Ztls::Cxn *tls);
/**
 * Returns if the context is a server context.
 */
int is_server(Ztls::Cxn *tls);
/**
 * returns per-record overhead
 */
ZtlsAPI size_t get_record_overhead(Ztls::Cxn *tls);
/**
 * sends an alert
 */
int send_alert(Ztls::Cxn *tls, buffer_t *sendbuf, uint8_t level, uint8_t description);
/**
 *
 */
int export_secret(Ztls::Cxn *tls, void *output, size_t outlen, const char *label, Bytes context_value, int is_early);
/**
 * build the body of a Certificate message. Can be called with tls set to NULL in order to create a precompressed message.
 */
int build_certificate_message(buffer_t *buf, Bytes request_context, Bytes *certificates,
                                   size_t num_certificates, Bytes ocsp_status);
/**
 *
 */
int calc_hash(hash_algorithm_t *algo, void *output, const void *src, size_t len);
/**
 *
 */
hash_context_t *hmac_create(hash_algorithm_t *algo, const void *key, size_t key_size);
/**
 *
 */
int hkdf_extract(hash_algorithm_t *hash, void *output, Bytes salt, Bytes ikm);
/**
 *
 */
int hkdf_expand(hash_algorithm_t *hash, void *output, size_t outlen, Bytes prk, Bytes info);
/**
 *
 */
int hkdf_expand_label(hash_algorithm_t *algo, void *output, size_t outlen, Bytes secret, const char *label,
                           Bytes hash_value, const char *label_prefix);
/**
 * instantiates a symmetric cipher
 */
cipher_context_t *cipher_new(cipher_algorithm_t *algo, int is_enc, const void *key);
/**
 * destroys a symmetric cipher
 */
void cipher_free(cipher_context_t *ctx);
/**
 * initializes the IV; this function must be called prior to calling cipher_encrypt
 */
static void cipher_init(cipher_context_t *ctx, const void *iv);
/**
 * Encrypts given text. The function must be used in a way that the output length would be equal to the input length. For example,
 * when using a block cipher in ECB mode, `len` must be a multiple of the block size when using a block cipher. The length can be
 * of any value when using a stream cipher or a block cipher in CTR mode.
 */
static void cipher_encrypt(cipher_context_t *ctx, void *output, const void *input, size_t len);
/**
 * instantiates an AEAD cipher given a secret, which is expanded using hkdf to a set of key and iv
 * @param aead
 * @param hash
 * @param is_enc 1 if creating a context for encryption, 0 if creating a context for decryption
 * @param secret the secret. The size must be the digest length of the hash algorithm
 * @return pointer to an AEAD context if successful, otherwise NULL
 */
aead_context_t *aead_new(aead_algorithm_t *aead, hash_algorithm_t *hash, int is_enc, const void *secret,
                                   const char *label_prefix);
/**
 * instantiates an AEAD cipher given key and iv
 * @param aead
 * @param is_enc 1 if creating a context for encryption, 0 if creating a context for decryption
 * @return pointer to an AEAD context if successful, otherwise NULL
 */
ZtlsAPI aead_context_t *aead_new_direct(aead_algorithm_t *aead, int is_enc, const void *key, const void *iv);
/**
 * destroys an AEAD cipher context
 */
ZtlsAPI void aead_free(aead_context_t *ctx);
/**
 * Permutes the static IV by applying given bytes using bit-wise XOR. This API can be used for supplying nonces longer than 64-
 * bits.
 */
void aead_xor_iv(aead_context_t *ctx, const void *bytes, size_t len);
static void aead_get_iv(aead_context_t *ctx, void *iv);
static void aead_set_iv(aead_context_t *ctx, const void *iv);
/**
 * Encrypts one AEAD block, writing ciphertext followed by the AEAD tag. `output` must have room for `inlen + tag_size` bytes.
 */
static size_t aead_encrypt(aead_context_t *ctx, void *output, const void *input, size_t inlen, uint64_t seq,
                                const void *aad, size_t aadlen);
/**
 * Encrypts one AEAD block, as well as one block of ECB (for QUIC / DTLS packet number encryption). Depending on the AEAD engine
 * being used, the two operations might run simultaneously.
 */
static void aead_encrypt_s(aead_context_t *ctx, void *output, const void *input, size_t inlen, uint64_t seq,
                                const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp);
/**
 * Encrypts one AEAD block from read-only plaintext vectors, writing the logically concatenated ciphertext followed by the AEAD tag.
 * `output` must have room for the sum of vector lengths plus `tag_size` bytes.
 */
static void aead_encrypt_v(aead_context_t *ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq,
                                const void *aad, size_t aadlen);
/**
 * Encrypts one AEAD block from read-only plaintext vectors, and optionally computes a supplementary cipher block after AEAD output
 * exists. Returns the sum of plaintext vector lengths plus the AEAD tag size. `output` must have room for that many bytes.
 * `supp == NULL` is valid and skips supplementary encryption. `input` may be NULL when `incnt` is zero; zero-length vectors are
 * accepted. For direct retained-buffer use, plaintext vector ranges must not overlap `output`. `supp->input` may point into
 * `output`, including the tag, and is read after the ciphertext-plus-tag output has been written.
 */
static size_t aead_encrypt_v_s(aead_context_t *ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq,
                                    const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp);
/**
 * Obsolete; new applications should use one of: `aead_encrypt`, `aead_encrypt_s`, `aead_encrypt_v`,
 * `aead_encrypt_v_s`.
 */
static void aead_encrypt_init(aead_context_t *ctx, uint64_t seq, const void *aad, size_t aadlen);
/**
 * Obsolete; see `aead_encrypt_init`.
 */
static size_t aead_encrypt_update(aead_context_t *ctx, void *output, const void *input, size_t inlen);
/**
 * Obsolete; see `aead_encrypt_init`.
 */
static size_t aead_encrypt_final(aead_context_t *ctx, void *output);
/**
 * decrypts an AEAD record
 * @return number of bytes emitted to output if successful, or SIZE_MAX if the input is invalid (e.g. broken MAC)
 */
static size_t aead_decrypt(aead_context_t *ctx, void *output, const void *input, size_t inlen, uint64_t seq,
                                const void *aad, size_t aadlen);
/**
 * internal
 */
void aead__build_iv(aead_algorithm_t *algo, uint8_t *iv, const uint8_t *static_iv, uint64_t seq);
/**
 *
 */
static void aead__do_encrypt(aead_context_t *ctx, void *output, const void *input, size_t inlen, uint64_t seq,
                                  const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp);
/**
 *
 */
static void aead__do_encrypt_v(aead_context_t *ctx, void *_output, const Bytes *input, size_t incnt, uint64_t seq,
                                    const void *aad, size_t aadlen);
/**
 * internal; suitable only for backends whose `do_encrypt_v` consumes vector input without full plaintext flattening and returns
 * after any required output-store visibility guarantees have been satisfied.
 */
static void aead__do_encrypt_v_s(aead_context_t *ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq,
                                      const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp);
/**
 * internal
 */
void _key_schedule_update_hash(key_schedule_t *sched, const uint8_t *msg, size_t msglen, int use_outer);
/**
 * checks if a server name is an IP address.
 */
int server_name_is_ipaddr(const char *name);
/**
 * encodes one ECH Config
 */
int ech_encode_config(buffer_t *buf, uint8_t config_id, hpke_kem_t *kem, Bytes public_key,
                           hpke_cipher_suite_t **ciphers, uint8_t max_name_length, const char *public_name);
/**
 * SetupBaseS function of RFC 9180. Given `kem`, `algo`, `info`, and receiver's public key, returns an ephemeral public key and an
 * AEAD context used for encrypting data.
 */
int hpke_setup_base_s(hpke_kem_t *kem, hpke_cipher_suite_t *cipher, KeyExchangeBytes *pk_s, aead_context_t **ctx,
                           Bytes pk_r, Bytes info);
/**
 * SetupBaseR function of RFC 9180. Given `kem`, `algo`, `info`, receiver's private key (`keyex`), and the esnder's public key,
 * returns the AEAD context to be used for decrypting data.
 */
int hpke_setup_base_r(hpke_kem_t *kem, hpke_cipher_suite_t *cipher, key_exchange_context_t *keyex,
                           aead_context_t **ctx, Bytes pk_s, Bytes info);

/**
 * the default get_time callback
 */
ZtlsExtern get_time_t get_time;
/**
 * default hash clone function that calls memcpy
 */
static void hash_clone_memcpy(void *dst, const void *src, size_t size);


inline Bytes wire_bytes(const void *data, size_t length)
{
    return {const_cast<uint8_t *>(static_cast<const uint8_t *>(data)), length};
}

inline void buffer_init(buffer_t *buf, void *smallbuf, size_t smallbuf_size, int tx)
{
    assert(tx == 0 || tx == 1);
    assert(smallbuf != NULL);
    buf->base = (uint8_t *)smallbuf;
    buf->capacity = smallbuf_size;
    buf->off = 0;
    buf->is_allocated = 0;
    buf->align_bits = 0;
    buf->tx = (uint8_t)tx;
    buf->record_sink = NULL;
}

inline void buffer_init_rx(buffer_t *buf, void *smallbuf, size_t smallbuf_size)
{
    buffer_init(buf, smallbuf, smallbuf_size, 0);
}

inline void buffer_init_tx(buffer_t *buf, void *smallbuf, size_t smallbuf_size)
{
    buffer_init(buf, smallbuf, smallbuf_size, 1);
}

inline void buffer_dispose(buffer_t *buf)
{
    buffer__release_memory(buf);
    *buf = buffer_t{};
}

inline void cipher_init(cipher_context_t *ctx, const void *iv)
{
    ctx->do_init(ctx, iv);
}

inline void cipher_encrypt(cipher_context_t *ctx, void *output, const void *input, size_t len)
{
    ctx->do_transform(ctx, output, input, len);
}

inline void aead_get_iv(aead_context_t *ctx, void *iv)
{
    ctx->do_get_iv(ctx, iv);
}

inline void aead_set_iv(aead_context_t *ctx, const void *iv)
{
    ctx->do_set_iv(ctx, iv);
}

inline size_t aead_encrypt(aead_context_t *ctx, void *output, const void *input, size_t inlen, uint64_t seq,
                                const void *aad, size_t aadlen)
{
    ctx->do_encrypt(ctx, output, input, inlen, seq, aad, aadlen, NULL);
    return inlen + ctx->algo->tag_size;
}

inline void aead_encrypt_s(aead_context_t *ctx, void *output, const void *input, size_t inlen, uint64_t seq,
                                const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp)
{
    ctx->do_encrypt(ctx, output, input, inlen, seq, aad, aadlen, supp);
}

inline void aead_encrypt_v(aead_context_t *ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq,
                                const void *aad, size_t aadlen)
{
    ctx->do_encrypt_v(ctx, output, input, incnt, seq, aad, aadlen);
}

static inline size_t aead__sum_iovec(const Bytes *input, size_t incnt)
{
    size_t inlen = 0;

    for (size_t i = 0; i != incnt; ++i) {
        if (input[i].length() > SIZE_MAX - inlen)
            abort();
        inlen += input[i].length();
    }
    return inlen;
}

inline size_t aead_encrypt_v_s(aead_context_t *ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq,
                                    const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp)
{
    size_t inlen = aead__sum_iovec(input, incnt);

    if (ctx->algo->tag_size > SIZE_MAX - inlen)
        abort();
    ctx->do_encrypt_v_s(ctx, output, input, incnt, seq, aad, aadlen, supp);
    return inlen + ctx->algo->tag_size;
}

inline void aead_encrypt_init(aead_context_t *ctx, uint64_t seq, const void *aad, size_t aadlen)
{
    ctx->do_encrypt_init(ctx, seq, aad, aadlen);
}

inline size_t aead_encrypt_update(aead_context_t *ctx, void *output, const void *input, size_t inlen)
{
    return ctx->do_encrypt_update(ctx, output, input, inlen);
}

inline size_t aead_encrypt_final(aead_context_t *ctx, void *output)
{
    return ctx->do_encrypt_final(ctx, output);
}

static inline void aead__do_supplementary(aead_supplementary_encryption_t *supp)
{
    if (supp != NULL) {
        cipher_init(supp->ctx, supp->input);
        memset(supp->output, 0, sizeof(supp->output));
        cipher_encrypt(supp->ctx, supp->output, supp->output, sizeof(supp->output));
    }
}

inline void aead__do_encrypt(aead_context_t *ctx, void *output, const void *input, size_t inlen, uint64_t seq,
                                  const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp)
{
    Bytes invec = wire_bytes(input, inlen);
    ctx->do_encrypt_v(ctx, output, &invec, 1, seq, aad, aadlen);

    aead__do_supplementary(supp);
}

inline void aead__do_encrypt_v(aead_context_t *ctx, void *_output, const Bytes *input, size_t incnt, uint64_t seq,
                                    const void *aad, size_t aadlen)
{
    uint8_t *output = (uint8_t *)_output;

    ctx->do_encrypt_init(ctx, seq, aad, aadlen);
    for (size_t i = 0; i < incnt; ++i)
        output += ctx->do_encrypt_update(ctx, output, input[i].data(), input[i].length());
    ctx->do_encrypt_final(ctx, output);
}

inline void aead__do_encrypt_v_s(aead_context_t *ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq,
                                      const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp)
{
    ctx->do_encrypt_v(ctx, output, input, incnt, seq, aad, aadlen);
    aead__do_supplementary(supp);
}

inline size_t aead_decrypt(aead_context_t *ctx, void *output, const void *input, size_t inlen, uint64_t seq,
                                const void *aad, size_t aadlen)
{
    return ctx->do_decrypt(ctx, output, input, inlen, seq, aad, aadlen);
}

inline void hash_clone_memcpy(void *dst, const void *src, size_t size)
{
    memcpy(dst, src, size);
}

#define Ztls_define_hash(name, ctx_type, init_func, update_func, final_func)                                                       \
    Ztls_define_hash6(name, ctx_type, init_func, update_func, final_func, hash_clone_memcpy)
#define Ztls_define_hash6(name, ctx_type, init_func, update_func, final_func, clone_func)                                          \
                                                                                                                                   \
    struct name##_context_t {                                                                                                      \
        hash_context_t super;                                                                                                 \
        ctx_type ctx;                                                                                                              \
    };                                                                                                                             \
    ZuDerive(name##_storage, (ZtArray<name##_context_t, ZtArrayHeapID<"Ztls.Engine.Hash." #name>>)); \
                                                                                                                                   \
    static void name##_update(hash_context_t *_ctx, const void *src, size_t len)                                              \
    {                                                                                                                              \
        struct name##_context_t *ctx = (struct name##_context_t *)_ctx;                                                            \
        update_func(&ctx->ctx, src, len);                                                                                          \
    }                                                                                                                              \
                                                                                                                                   \
    static void name##_final(hash_context_t *_ctx, void *md, hash_final_mode_t mode)                                     \
    {                                                                                                                              \
        struct name##_context_t *ctx = (struct name##_context_t *)_ctx;                                                            \
        if (mode == HASH_FINAL_MODE_SNAPSHOT) {                                                                               \
            ctx_type copy = ctx->ctx;                                                                                              \
            final_func(&copy, md);                                                                                                 \
            ZuClear(&copy, sizeof(copy));                                                                                \
            return;                                                                                                                \
        }                                                                                                                          \
        if (md != NULL)                                                                                                            \
            final_func(&ctx->ctx, md);                                                                                             \
        switch (mode) {                                                                                                            \
        case HASH_FINAL_MODE_FREE:                                                                                            \
            ZuClear(&ctx->ctx, sizeof(ctx->ctx));                                                                        \
            name##_storage::free(ctx);                                                                                                             \
            break;                                                                                                                 \
        case HASH_FINAL_MODE_RESET:                                                                                           \
            init_func(&ctx->ctx);                                                                                                  \
            break;                                                                                                                 \
        default:                                                                                                                   \
            assert(!"FIXME");                                                                                                      \
            break;                                                                                                                 \
        }                                                                                                                          \
    }                                                                                                                              \
                                                                                                                                   \
    static hash_context_t *name##_clone(hash_context_t *_src)                                                            \
    {                                                                                                                              \
        struct name##_context_t *dst, *src = (struct name##_context_t *)_src;                                                      \
        name##_storage storage; \
        storage.length(1); \
        dst = ZuMv(storage).release(); \
        dst->super = src->super;                                                                                                   \
        clone_func(&dst->ctx, &src->ctx, sizeof(dst->ctx));                                                                        \
        return &dst->super;                                                                                                        \
    }                                                                                                                              \
                                                                                                                                   \
    static hash_context_t *name##_create(void)                                                                                \
    {                                                                                                                              \
        struct name##_context_t *ctx;                                                                                              \
        name##_storage storage; \
        storage.length(1); \
        ctx = ZuMv(storage).release(); \
        ctx->super = (hash_context_t){name##_update, name##_final, name##_clone};                                             \
        init_func(&ctx->ctx);                                                                                                      \
        return &ctx->super;                                                                                                        \
    }

} // Ztls_


#endif
