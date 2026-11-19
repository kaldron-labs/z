/*
 * Copyright (c) 2020 Fastly, Kazuho Oku
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
#ifndef ZtlsFusion_HH
#define ZtlsFusion_HH

#include <stddef.h>
#include <emmintrin.h>
#include <immintrin.h>
#include <zlib/ZtlsEngine.hh>

namespace Ztls_ {

#define Ztls_FUSION_AES128_ROUNDS 10
#define Ztls_FUSION_AES256_ROUNDS 14

#ifndef Ztls_X86_CACHE_LINE_ALIGN_BITS
#define Ztls_X86_CACHE_LINE_ALIGN_BITS 6 /* 64-bytes */
#endif

typedef struct fusion_aesecb_context {
    union {
        __m128i *m128;
        __m256i *m256;
    } keys;
    unsigned rounds;
    uint8_t aesni256;
} __attribute__((aligned(32))) fusion_aesecb_context_t;

typedef struct fusion_aesgcm_context fusion_aesgcm_context_t;

void fusion_aesecb_init(fusion_aesecb_context_t *ctx, int is_enc, const void *key, size_t key_size, int avx256);
void fusion_aesecb_dispose(fusion_aesecb_context_t *ctx);
void fusion_aesecb_encrypt(fusion_aesecb_context_t *ctx, void *dst, const void *src);

/**
 * Creates an AES-GCM context.
 * @param key       the AES key (128 bits)
 * @param capacity  maximum size of AEAD record (i.e. AAD + encrypted payload)
 */
fusion_aesgcm_context_t *fusion_aesgcm_new(const void *key, size_t key_size, size_t capacity);
/**
 * Updates the capacity.
 */
fusion_aesgcm_context_t *fusion_aesgcm_set_capacity(fusion_aesgcm_context_t *ctx, size_t capacity);
/**
 * Destroys an AES-GCM context.
 */
void fusion_aesgcm_free(fusion_aesgcm_context_t *ctx);
/**
 * Encrypts an AEAD block, and in parallel, optionally encrypts one block using AES-ECB.
 * @param ctx      context
 * @param output   output buffer
 * @param input    payload to be encrypted
 * @param inlen    size of the payload to be encrypted
 * @param counter
 * @param aad      AAD
 * @param aadlen   size of AAD
 * @param supp     (optional) supplementary encryption context
 */
void fusion_aesgcm_encrypt(fusion_aesgcm_context_t *ctx, void *output, const void *input, size_t inlen, __m128i ctr,
                                const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp);
/**
 * Decrypts an AEAD block, an in parallel, optionally encrypts one block using AES-ECB. Returns if decryption was successful.
 * @param iv       initialization vector of 12 bytes
 * @param output   output buffer
 * @param input    payload to be decrypted
 * @param inlen    size of the payload to be decrypted
 * @param aad      AAD
 * @param aadlen   size of AAD
 * @param tag      the AEAD tag being received from peer
 */
int fusion_aesgcm_decrypt(fusion_aesgcm_context_t *ctx, void *output, const void *input, size_t inlen, __m128i ctr,
                               const void *aad, size_t aadlen, const void *tag);

/** Checked, bounded serial AES-GCM encryption. Destinations may be unaligned.
 * begin initializes one AEAD operation; chunk emits exactly length bytes; end emits its tag.
 * A callback error clears the serial state and makes that operation terminal.
 */
int fusion_aead_enc_begin(aead_context_t *ctx, uint64_t seq, const void *aad, size_t aadlen);
int fusion_aead_enc_chunk(aead_context_t *ctx, void *output, const void *input, size_t length, size_t *written);
int fusion_aead_enc_end(aead_context_t *ctx, void *output, size_t capacity, size_t *written);

/** Serial decryption writes provisional plaintext into the checked destination.
 * Only a successful end authenticates it; abort clears an unfinished operation. */
int fusion_aead_dec_begin(aead_context_t *ctx, uint64_t seq, const void *aad, size_t aadlen);
int fusion_aead_dec_chunk(aead_context_t *ctx, void *output, size_t capacity,
                              const void *input, size_t length, size_t *written);
int fusion_aead_dec_end(aead_context_t *ctx, const void *tag, size_t taglen);
void fusion_aead_dec_abort(aead_context_t *ctx);

/**
 * A boolean flag indicating if vaes and vpclmulqdq (256-bit crypto instructions) should be used. This flag is set automatically
 * when `fusion_is_supported_by_cpu` is called. Users can update the flag to enforce behavior. Engines that do not have support
 * for these 256-bit instructions will continue using the 128-bit ones, even when this flag is set.
 */
extern int fusion_can_aesni256;
/**
 * AES-CTR backends. These backends can only handle ciphertext up to 16 bytes long.
 */
extern cipher_algorithm_t fusion_aes128ctr, fusion_aes256ctr;
/**
 * AES-GCM backends optimized for for QUIC. These engines require sufficient amount of memory to be available; they raise assertion
 * failure if context memory cannot be allocated.
 */
ZtlsExtern aead_algorithm_t fusion_aes128gcm, fusion_aes256gcm;
/**
 * AES-GCM backend optimized for TLS. This backend writes ciphertext using non-temporal store instructions to avoid polluting CPU
 * caches with data that is typically consumed by NIC DMA and not reused by the CPU. This can be beneficial on systems where DMA
 * reads only from main memory, such as CPUs without Intel DDIO support. Although non-temporal stores have lower throughput than
 * regular cached stores, that cost is usually hidden by AES-GCM computation.
 */
extern aead_algorithm_t non_temporal_aes128gcm, non_temporal_aes256gcm;
/**
 * Implements the 4-pass Feistel Network backed by AES-ECB, used for encrypting the Connection ID in draft-ietf-quic-load-balancers.
 */

/**
 * Returns a boolean indicating if fusion can be used.
 */
ZtlsAPI int fusion_is_supported_by_cpu(void);

} // Ztls_

#endif
