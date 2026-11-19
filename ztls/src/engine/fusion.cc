/*
 * This source file is licensed under the Apache License 2.0 *and* the MIT
 * License. Please agree to *both* of the licensing terms!
 *
 *
 * `transformH` function is a derivative work of OpenSSL. The original work
 * is covered by the following license:
 *
 * Copyright 2013-2020 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 *
 * All other work, including modifications to the `transformH` function is
 * covered by the following MIT license:
 *
 * Copyright (c) 2020-2022 Fastly, Kazuho Oku
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
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)

#include <stdint.h>

#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#include <tmmintrin.h>
#include <nmmintrin.h>
#include <wmmintrin.h>
#include <zlib/ZmVHeap.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtlsEngine.hh>
#include <zlib/ZtlsFusion.hh>

#include <zlib/ZtlsSec.hh>

namespace Ztls_ {

#if defined(__clang__)
#if __has_feature(address_sanitizer)
#define NO_SANITIZE_ADDRESS __attribute__((no_sanitize("address")))
#endif
#elif __SANITIZE_ADDRESS__ /* gcc */
#define NO_SANITIZE_ADDRESS __attribute__((no_sanitize_address))
#endif
#ifndef NO_SANITIZE_ADDRESS
#define NO_SANITIZE_ADDRESS
#endif

struct fusion_aesgcm_context {
    fusion_aesecb_context_t ecb;
    size_t capacity;
    size_t ghash_cnt;
};

struct fusion_aesgcm_ghash_precompute128 {
    __m128i H;
    __m128i r;
};

ZuDerive(Ghash128, (ZtArray<fusion_aesgcm_ghash_precompute128, ZtArrayHeapID<"Ztls.Fusion.GHASH128">>));

struct fusion_aesgcm_context128 {
    struct fusion_aesgcm_context super;
    Ghash128 ghash;
};

union fusion_aesgcm_ghash_precompute256 {
    struct {
        __m128i H[2];
        __m128i r[2];
    };
    struct {
        __m256i Hx2;
        __m256i rx2;
    };
};

ZuDerive(Ghash256, (ZtArray<fusion_aesgcm_ghash_precompute256, ZtArrayHeapID<"Ztls.Fusion.GHASH256">>));

struct fusion_aesgcm_context256 {
    struct fusion_aesgcm_context super;
    Ghash256 ghash;
};

static_assert(__is_standard_layout(fusion_aesgcm_context128));
static_assert(__is_standard_layout(fusion_aesgcm_context256));
using FusionGcmHeap = ZmVHeap<"Ztls.Fusion.GCM", 64, 4096, alignof(fusion_aesgcm_context_t)>;

struct ctr_context {
    cipher_context_t super;
    fusion_aesecb_context_t fusion;
    __m128i bits;
    uint8_t is_ready;
};

struct aesgcm_context {
    aead_context_t super;
    fusion_aesgcm_context_t *aesgcm;
    /**
     * retains the static IV in the upper 96 bits (in little endian)
     */
    __m128i static_iv;
    struct {
        __m128i j0, ctr, hash;
        uint8_t bits[16], carry[16];
        size_t bit_used, carry_used;
        uint64_t aadlen, textlen;
        int active;
    } serial;
};

static const uint64_t poly_[2] __attribute__((aligned(16))) = {1, 0xc200000000000000};
#define poly (*(__m128i *)poly_)
static const uint8_t byteswap_[32] __attribute__((aligned(32))) = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
                                                                   15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
#define byteswap128 (*(__m128i *)byteswap_)
#define byteswap256 (*(__m256i *)byteswap_)
static const uint8_t one_[16] __attribute__((aligned(16))) = {1};
#define one8 (*(__m128i *)one_)
static const uint8_t incr128x2_[32] __attribute__((aligned(32))) = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
#define incr128x2 (*(__m256i *)incr128x2_)

/* This function is covered by the Apache License and the MIT License. The origin is crypto/modes/asm/ghash-x86_64.pl of openssl
 * at commit 33388b4. */
static __m128i transformH(__m128i H)
{
    //  # <<1 twist
    //  pshufd          \$0b11111111,$Hkey,$T2  # broadcast uppermost dword
    __m128i t2 = _mm_shuffle_epi32(H, 0xff);
    // movdqa          $Hkey,$T1
    __m128i t1 = H;
    // psllq           \$1,$Hkey
    H = _mm_slli_epi64(H, 1);
    // pxor            $T3,$T3                 #
    __m128i t3 = _mm_setzero_si128();
    // psrlq           \$63,$T1
    t1 = _mm_srli_epi64(t1, 63);
    // pcmpgtd         $T2,$T3                 # broadcast carry bit
    t3 = _mm_cmplt_epi32(t2, t3);
    //     pslldq          \$8,$T1
    t1 = _mm_slli_si128(t1, 8);
    // por             $T1,$Hkey               # H<<=1
    H = _mm_or_si128(t1, H);

    // # magic reduction
    // pand            .L0x1c2_polynomial(%rip),$T3
    t3 = _mm_and_si128(t3, poly);
    // pxor            $T3,$Hkey               # if(carry) H^=0x1c2_polynomial
    H = _mm_xor_si128(t3, H);

    return H;
}
// end of Apache License code

static __m128i gfmul(__m128i x, __m128i y)
{
    __m128i lo = _mm_clmulepi64_si128(x, y, 0x00);
    __m128i hi = _mm_clmulepi64_si128(x, y, 0x11);

    __m128i a = _mm_shuffle_epi32(x, 78);
    __m128i b = _mm_shuffle_epi32(y, 78);
    a = _mm_xor_si128(a, x);
    b = _mm_xor_si128(b, y);

    a = _mm_clmulepi64_si128(a, b, 0x00);
    a = _mm_xor_si128(a, lo);
    a = _mm_xor_si128(a, hi);

    b = _mm_slli_si128(a, 8);
    a = _mm_srli_si128(a, 8);

    lo = _mm_xor_si128(lo, b);
    hi = _mm_xor_si128(hi, a);

    // from https://crypto.stanford.edu/RealWorldCrypto/slides/gueron.pdf
    __m128i t = _mm_clmulepi64_si128(lo, poly, 0x10);
    lo = _mm_shuffle_epi32(lo, 78);
    lo = _mm_xor_si128(lo, t);
    t = _mm_clmulepi64_si128(lo, poly, 0x10);
    lo = _mm_shuffle_epi32(lo, 78);
    lo = _mm_xor_si128(lo, t);

    return _mm_xor_si128(hi, lo);
}

static inline __m128i gfmul_do_reduce(__m128i hi, __m128i lo, __m128i mid)
{
    mid = _mm_xor_si128(mid, hi);
    mid = _mm_xor_si128(mid, lo);
    lo = _mm_xor_si128(lo, _mm_slli_si128(mid, 8));
    hi = _mm_xor_si128(hi, _mm_srli_si128(mid, 8));

    /* fast reduction, using https://crypto.stanford.edu/RealWorldCrypto/slides/gueron.pdf */
    __m128i r = _mm_clmulepi64_si128(lo, poly, 0x10);
    lo = _mm_shuffle_epi32(lo, 78);
    lo = _mm_xor_si128(lo, r);
    r = _mm_clmulepi64_si128(lo, poly, 0x10);
    lo = _mm_shuffle_epi32(lo, 78);
    lo = _mm_xor_si128(lo, r);
    lo = _mm_xor_si128(hi, lo);

    return lo;
}

struct fusion_gfmul_state128 {
    __m128i hi, lo, mid;
};

#if defined(__GNUC__) && !defined(__clang__)
static inline __m128i xor128(__m128i x, __m128i y)
{
    __m128i ret;
    __asm__("vpxor %2, %1, %0" : "=x"(ret) : "x"(x), "xm"(y));
    return ret;
}
#else
#define xor128 _mm_xor_si128
#endif

static inline void gfmul_do_step128(struct fusion_gfmul_state128 *gstate, __m128i X,
                                    struct fusion_aesgcm_ghash_precompute128 *precompute)
{
    __m128i t1 = _mm_clmulepi64_si128(precompute->H, X, 0x00);
    __m128i t2 = _mm_clmulepi64_si128(precompute->H, X, 0x11);
    __m128i t3 = _mm_shuffle_epi32(X, 78);
    t3 = _mm_xor_si128(t3, X);
    t3 = _mm_clmulepi64_si128(precompute->r, t3, 0x00);
    gstate->lo = xor128(gstate->lo, t1);
    gstate->hi = xor128(gstate->hi, t2);
    gstate->mid = xor128(gstate->mid, t3);
}

#undef xor128

static inline void gfmul_firststep128(struct fusion_gfmul_state128 *gstate, __m128i X,
                                      struct fusion_aesgcm_ghash_precompute128 *precompute)
{
    X = _mm_shuffle_epi8(X, byteswap128);
    X = _mm_xor_si128(gstate->lo, X);
    gstate->lo = _mm_setzero_si128();
    gstate->hi = _mm_setzero_si128();
    gstate->mid = _mm_setzero_si128();
    gfmul_do_step128(gstate, X, precompute);
}

static inline void gfmul_nextstep128(struct fusion_gfmul_state128 *gstate, __m128i X,
                                     struct fusion_aesgcm_ghash_precompute128 *precompute)
{
    X = _mm_shuffle_epi8(X, byteswap128);
    gfmul_do_step128(gstate, X, precompute);
}

static inline void gfmul_reduce128(struct fusion_gfmul_state128 *gstate)
{
    gstate->lo = gfmul_do_reduce(gstate->hi, gstate->lo, gstate->mid);
}

static inline __m128i gfmul_get_tag128(struct fusion_gfmul_state128 *gstate, __m128i ek0)
{
    __m128i tag = _mm_shuffle_epi8(gstate->lo, byteswap128);
    tag = _mm_xor_si128(tag, ek0);
    return tag;
}

struct fusion_gfmul_state256 {
    __m256i hi, lo, mid;
};

static inline void gfmul_do_step256(struct fusion_gfmul_state256 *gstate, __m256i X,
                                    union fusion_aesgcm_ghash_precompute256 *precompute)
{
    __m256i t = _mm256_clmulepi64_epi128(precompute->Hx2, X, 0x00);
    gstate->lo = _mm256_xor_si256(gstate->lo, t);
    t = _mm256_clmulepi64_epi128(precompute->Hx2, X, 0x11);
    gstate->hi = _mm256_xor_si256(gstate->hi, t);
    t = _mm256_shuffle_epi32(X, 78);
    t = _mm256_xor_si256(t, X);
    t = _mm256_clmulepi64_epi128(precompute->rx2, t, 0x00);
    gstate->mid = _mm256_xor_si256(gstate->mid, t);
}

static inline void gfmul_firststep256(struct fusion_gfmul_state256 *gstate, __m256i X, int half,
                                      union fusion_aesgcm_ghash_precompute256 *precompute)
{
    X = _mm256_shuffle_epi8(X, byteswap256);
    X = _mm256_xor_si256(gstate->lo, X);
    if (half)
        X = _mm256_permute2f128_si256(X, X, 0x08);
    gstate->lo = _mm256_setzero_si256();
    gstate->hi = _mm256_setzero_si256();
    gstate->mid = _mm256_setzero_si256();
    gfmul_do_step256(gstate, X, precompute);
}

static inline void gfmul_nextstep256(struct fusion_gfmul_state256 *gstate, __m256i X,
                                     union fusion_aesgcm_ghash_precompute256 *precompute)
{
    X = _mm256_shuffle_epi8(X, byteswap256);
    gfmul_do_step256(gstate, X, precompute);
}

static inline void gfmul_reduce256(struct fusion_gfmul_state256 *gstate)
{
#define XOR_256TO128(y) _mm_xor_si128(_mm256_castsi256_si128(y), _mm256_extractf128_si256((y), 1))
    __m128i hi = XOR_256TO128(gstate->hi);
    __m128i lo = XOR_256TO128(gstate->lo);
    __m128i mid = XOR_256TO128(gstate->mid);
#undef XOR_256TO128

    lo = gfmul_do_reduce(hi, lo, mid);
    gstate->lo = _mm256_zextsi128_si256(lo);
}

static inline __m128i gfmul_get_tag256(struct fusion_gfmul_state256 *gstate, __m128i ek0)
{
    __m128i tag = _mm_shuffle_epi8(_mm256_castsi256_si128(gstate->lo), byteswap128);
    tag = _mm_xor_si128(tag, ek0);
    return tag;
}

static inline __m128i aesecb_encrypt(fusion_aesecb_context_t *ctx, __m128i v)
{
#define ROUNDKEY(i) (ctx->aesni256 ? _mm256_castsi256_si128(ctx->keys.m256[i]) : ctx->keys.m128[i])

    v = _mm_xor_si128(v, ROUNDKEY(0));
    for (size_t i = 1; i < ctx->rounds; ++i)
        v = _mm_aesenc_si128(v, ROUNDKEY(i));
    v = _mm_aesenclast_si128(v, ROUNDKEY(ctx->rounds));

    return v;

#undef ROUNDKEY
}

// 32-bytes of 0xff followed by 31-bytes of 0x00
static const uint8_t loadn_mask[63] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t loadn_shuffle[31] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                          0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, // first 16 bytes map to byte offsets
                                          0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                                          0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}; // latter 15 bytes map to zero

NO_SANITIZE_ADDRESS
static inline __m128i loadn_end_of_page(const void *p, size_t l)
{
    uintptr_t shift = (uintptr_t)p & 15;
    __m128i pattern = _mm_loadu_si128((const __m128i *)(loadn_shuffle + shift));
    return _mm_shuffle_epi8(_mm_load_si128((const __m128i *)((uintptr_t)p - shift)), pattern);
}

NO_SANITIZE_ADDRESS
static inline __m128i loadn128(const void *p, size_t l)
{
    __m128i v, mask = _mm_loadu_si128((__m128i *)(loadn_mask + 32 - l));
    uintptr_t mod4k = (uintptr_t)p % 4096;

    if (Ztls_LIKELY(mod4k <= 4096 - 16) || mod4k + l > 4096) {
        v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p));
    } else {
        v = loadn_end_of_page(p, l);
    }
    v = _mm_and_si128(v, mask);

    return v;
}

NO_SANITIZE_ADDRESS
static inline __m256i loadn256(const void *p, size_t l)
{
    __m256i v, mask = _mm256_loadu_si256((__m256i *)(loadn_mask + 32 - l));
    uintptr_t mod4k = (uintptr_t)p % 4096;

    if (Ztls_LIKELY(mod4k < 4096 - 32) || mod4k + l > 4096) {
        v = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
    } else if (l > 16) {
        __m128i first16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p)), second16 = loadn128((uint8_t *)p + 16, l - 16);
        v = _mm256_permute2f128_si256(_mm256_castsi128_si256(first16), _mm256_castsi128_si256(second16), 0x20);
    } else if (l == 16) {
        v = _mm256_castsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i *>(p)));
    } else {
        v = _mm256_castsi128_si256(loadn_end_of_page(p, l));
    }
    v = _mm256_and_si256(v, mask);

    return v;
}

static inline void storen128(void *_p, size_t l, __m128i v)
{
    uint8_t buf[16], *p = static_cast<uint8_t *>(_p);

    *(__m128i *)buf = v;

    for (size_t i = 0; i != l; ++i)
        p[i] = buf[i];
}

struct fusion_input {
    const uint8_t *ptr;
    size_t len;
    const Bytes *vec;
    size_t vec_index;
    size_t vec_count;
};

static inline struct fusion_input fusion_input_iovec(const Bytes *input, size_t incnt)
{
    return (struct fusion_input){NULL, 0, input, 0, incnt};
}

static inline int fusion_input_next(struct fusion_input *src)
{
    while (src->len == 0 && src->vec_index != src->vec_count) {
        src->ptr = src->vec[src->vec_index].data();
        src->len = src->vec[src->vec_index].length();
        ++src->vec_index;
    }
    return src->len != 0;
}

static size_t fusion_input_read(struct fusion_input *src, uint8_t *dst, size_t max)
{
    size_t off = 0;

    while (off != max && fusion_input_next(src)) {
        size_t len = max - off;
        if (len > src->len)
            len = src->len;
        memcpy(dst + off, src->ptr, len);
        src->ptr += len;
        src->len -= len;
        off += len;
    }
    return off;
}

static inline void fusion_input_consume(struct fusion_input *src, size_t len)
{
    src->ptr += len;
    src->len -= len;
}

static inline void fusion_ghash_update128(struct fusion_aesgcm_context128 *ctx, struct fusion_gfmul_state128 *gstate,
                                          __m128i block)
{
    gfmul_firststep128(gstate, block, ctx->ghash.data());
    gfmul_reduce128(gstate);
}

static void fusion_aesgcm_encrypt_iter(fusion_aesgcm_context_t *_ctx, void *output, struct fusion_input *input, size_t inlen,
                                       __m128i ctr, const void *_aad, size_t aadlen)
{
    struct fusion_aesgcm_context128 *ctx = reinterpret_cast<struct fusion_aesgcm_context128 *>(_ctx);
    const uint8_t *aad = static_cast<const uint8_t *>(_aad);
    uint8_t *dst = static_cast<uint8_t *>(output);
    struct fusion_gfmul_state128 gstate = {0};
    size_t aadlen_total = aadlen;

    assert(!_ctx->ecb.aesni256);

    while (aadlen >= 16) {
        fusion_ghash_update128(ctx, &gstate, _mm_loadu_si128((const __m128i *)aad));
        aad += 16;
        aadlen -= 16;
    }
    if (aadlen != 0)
        fusion_ghash_update128(ctx, &gstate, loadn128(aad, aadlen));

    ctr = _mm_insert_epi32(ctr, 1, 0);
    __m128i ek0 = aesecb_encrypt(&ctx->super.ecb, _mm_shuffle_epi8(ctr, byteswap128));

    for (size_t left = inlen; left != 0;) {
        ctr = _mm_add_epi64(ctr, one8);
        __m128i bits = aesecb_encrypt(&ctx->super.ecb, _mm_shuffle_epi8(ctr, byteswap128)), enc;
        size_t blocklen;

        if (fusion_input_next(input) && input->len >= 16) {
            enc = _mm_xor_si128(_mm_loadu_si128((const __m128i *)input->ptr), bits);
            _mm_storeu_si128((__m128i *)dst, enc);
            fusion_input_consume(input, 16);
            blocklen = 16;
        } else {
            uint8_t block[16] = {0}, encbuf[16] = {0};

            blocklen = fusion_input_read(input, block, left < 16 ? left : 16);
            assert(blocklen != 0);
            enc = _mm_xor_si128(_mm_loadu_si128((const __m128i *)block), bits);
            storen128(dst, blocklen, enc);
            storen128(encbuf, blocklen, enc);
            enc = _mm_loadu_si128((const __m128i *)encbuf);
        }

        fusion_ghash_update128(ctx, &gstate, enc);
        dst += blocklen;
        left -= blocklen;
    }

    __m128i ac =
        _mm_shuffle_epi8(_mm_set_epi32(0, (int)aadlen_total * 8, 0, (int)inlen * 8), byteswap128);
    fusion_ghash_update128(ctx, &gstate, ac);
    _mm_storeu_si128((__m128i *)dst, gfmul_get_tag128(&gstate, ek0));
}

void fusion_aesgcm_encrypt(fusion_aesgcm_context_t *_ctx, void *output, const void *input, size_t inlen, __m128i ctr,
                                const void *_aad, size_t aadlen, aead_supplementary_encryption_t *supp)
{
/* init the bits (we can always run in full), but use the last slot for calculating ek0, if possible */
#define AESECB6_INIT()                                                                                                             \
    do {                                                                                                                           \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits0 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits1 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits2 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits3 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits4 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        if (Ztls_LIKELY(srclen > 16 * 5)) {                                                                                        \
            ctr = _mm_add_epi64(ctr, one8);                                                                                        \
            bits5 = _mm_shuffle_epi8(ctr, byteswap128);                                                                            \
        } else {                                                                                                                   \
            if ((state & STATE_EK0_BEEN_FED) == 0) {                                                                               \
                bits5 = ek0;                                                                                                       \
                state |= STATE_EK0_BEEN_FED;                                                                                       \
            }                                                                                                                      \
            if ((state & STATE_SUPP_USED) != 0 && srclen <= 16 * 4 && (const __m128i *)supp->input + 1 <= dst_ghash) {             \
                bits4 = _mm_loadu_si128(static_cast<const __m128i *>(supp->input));                                                                              \
                bits4keys = ((struct ctr_context *)supp->ctx)->fusion.keys.m128;                                                   \
                state |= STATE_SUPP_IN_PROCESS;                                                                                    \
            }                                                                                                                      \
        }                                                                                                                          \
        __m128i k = ctx->super.ecb.keys.m128[0];                                                                                   \
        bits0 = _mm_xor_si128(bits0, k);                                                                                           \
        bits1 = _mm_xor_si128(bits1, k);                                                                                           \
        bits2 = _mm_xor_si128(bits2, k);                                                                                           \
        bits3 = _mm_xor_si128(bits3, k);                                                                                           \
        bits4 = _mm_xor_si128(bits4, bits4keys[0]);                                                                                \
        bits5 = _mm_xor_si128(bits5, k);                                                                                           \
    } while (0)

/* aes block update */
#define AESECB6_UPDATE(i)                                                                                                          \
    do {                                                                                                                           \
        __m128i k = ctx->super.ecb.keys.m128[i];                                                                                   \
        bits0 = _mm_aesenc_si128(bits0, k);                                                                                        \
        bits1 = _mm_aesenc_si128(bits1, k);                                                                                        \
        bits2 = _mm_aesenc_si128(bits2, k);                                                                                        \
        bits3 = _mm_aesenc_si128(bits3, k);                                                                                        \
        bits4 = _mm_aesenc_si128(bits4, bits4keys[i]);                                                                             \
        bits5 = _mm_aesenc_si128(bits5, k);                                                                                        \
    } while (0)

/* aesenclast */
#define AESECB6_FINAL(i)                                                                                                           \
    do {                                                                                                                           \
        __m128i k = ctx->super.ecb.keys.m128[i];                                                                                   \
        bits0 = _mm_aesenclast_si128(bits0, k);                                                                                    \
        bits1 = _mm_aesenclast_si128(bits1, k);                                                                                    \
        bits2 = _mm_aesenclast_si128(bits2, k);                                                                                    \
        bits3 = _mm_aesenclast_si128(bits3, k);                                                                                    \
        bits4 = _mm_aesenclast_si128(bits4, bits4keys[i]);                                                                         \
        bits5 = _mm_aesenclast_si128(bits5, k);                                                                                    \
    } while (0)

    struct fusion_aesgcm_context128 *ctx = reinterpret_cast<struct fusion_aesgcm_context128 *>(_ctx);
    __m128i ek0, bits0, bits1, bits2, bits3, bits4, bits5 = _mm_setzero_si128();
    const __m128i *bits4keys = ctx->super.ecb.keys.m128; /* is changed to supp->ctx.keys when calcurating suppout */
    struct fusion_gfmul_state128 gstate = {0};
    __m128i gdatabuf[6];
    __m128i ac = _mm_shuffle_epi8(_mm_set_epi32(0, (int)aadlen * 8, 0, (int)inlen * 8), byteswap128);

    // src and dst are updated after the chunk is processed
    const __m128i *src = static_cast<const __m128i *>(input);
    __m128i *dst = static_cast<__m128i *>(output);
    size_t srclen = inlen;
    // aad and src_ghash are updated before the chunk is processed (i.e., when the pointers are fed indo the processor)
    const __m128i *aad = static_cast<const __m128i *>(_aad), *dst_ghash = dst;
    size_t dst_ghashlen = srclen;

    struct fusion_aesgcm_ghash_precompute128 *ghash_precompute = ctx->ghash.data() + (aadlen + 15) / 16 + (srclen + 15) / 16 + 1;

#define STATE_EK0_BEEN_FED 0x3
#define STATE_EK0_INCOMPLETE 0x2
#define STATE_EK0_READY() ((state & STATE_EK0_BEEN_FED) == 0x1)
#define STATE_SUPP_USED 0x4
#define STATE_SUPP_IN_PROCESS 0x8
    int32_t state = supp != NULL ? STATE_SUPP_USED : 0;

    /* build counter */
    ctr = _mm_insert_epi32(ctr, 1, 0);
    ek0 = _mm_shuffle_epi8(ctr, byteswap128);

    /* start preparing AES */
    AESECB6_INIT();
    AESECB6_UPDATE(1);

    /* build first ghash data (only AAD can be fed at this point, as this would be calculated alongside the first AES block) */
    const __m128i *gdata = gdatabuf; // points to the elements fed into GHASH
    size_t gdata_cnt = 0;
    if (Ztls_LIKELY(aadlen != 0)) {
        while (gdata_cnt < 6) {
            if (Ztls_LIKELY(aadlen < 16)) {
                if (aadlen != 0) {
                    gdatabuf[gdata_cnt++] = loadn128(aad, aadlen);
                    aadlen = 0;
                }
                goto MainLoop;
            }
            gdatabuf[gdata_cnt++] = _mm_loadu_si128(aad++);
            aadlen -= 16;
        }
    }

    /* the main loop */
MainLoop:
    while (1) {
        /* run AES and multiplication in parallel */
        size_t i;
        for (i = 2; i < gdata_cnt + 2; ++i) {
            AESECB6_UPDATE(i);
            gfmul_nextstep128(&gstate, _mm_loadu_si128(gdata++), --ghash_precompute);
        }
        for (; i < ctx->super.ecb.rounds; ++i)
            AESECB6_UPDATE(i);
        AESECB6_FINAL(i);

        /* apply the bit stream to src and write to dest */
        if (Ztls_LIKELY(srclen >= 6 * 16)) {
#define APPLY(i) _mm_storeu_si128(dst + i, _mm_xor_si128(_mm_loadu_si128(src + i), bits##i))
            APPLY(0);
            APPLY(1);
            APPLY(2);
            APPLY(3);
            APPLY(4);
            APPLY(5);
#undef APPLY
            dst += 6;
            src += 6;
            srclen -= 6 * 16;
        } else {
            if ((state & STATE_EK0_BEEN_FED) == STATE_EK0_BEEN_FED) {
                ek0 = bits5;
                state &= ~STATE_EK0_INCOMPLETE;
            }
            if ((state & STATE_SUPP_IN_PROCESS) != 0) {
                _mm_storeu_si128((__m128i *)supp->output, bits4);
                state &= ~(STATE_SUPP_USED | STATE_SUPP_IN_PROCESS);
            }
            if (srclen != 0) {
#define APPLY(i)                                                                                                                   \
    do {                                                                                                                           \
        if (Ztls_LIKELY(srclen >= 16)) {                                                                                           \
            _mm_storeu_si128(dst++, _mm_xor_si128(_mm_loadu_si128(src++), bits##i));                                               \
            srclen -= 16;                                                                                                          \
        } else if (Ztls_LIKELY(srclen != 0)) {                                                                                     \
            bits0 = bits##i;                                                                                                       \
            goto ApplyRemainder;                                                                                                   \
        } else {                                                                                                                   \
            goto ApplyEnd;                                                                                                         \
        }                                                                                                                          \
    } while (0)
                APPLY(0);
                APPLY(1);
                APPLY(2);
                APPLY(3);
                APPLY(4);
                APPLY(5);
#undef APPLY
                goto ApplyEnd;
            ApplyRemainder:
                storen128(dst, srclen, _mm_xor_si128(loadn128(src, srclen), bits0));
                dst = (__m128i *)((uint8_t *)dst + srclen);
                srclen = 0;
            ApplyEnd:;
            }
        }

        /* next block AES starts here */
        AESECB6_INIT();

        AESECB6_UPDATE(1);

        /* setup gdata */
        if (Ztls_UNLIKELY(aadlen != 0)) {
            gdata_cnt = 0;
            while (gdata_cnt < 6) {
                if (aadlen < 16) {
                    if (aadlen != 0) {
                        gdatabuf[gdata_cnt++] = loadn128(aad, aadlen);
                        aadlen = 0;
                    }
                    goto GdataFillDST;
                }
                gdatabuf[gdata_cnt++] = _mm_loadu_si128(aad++);
                aadlen -= 16;
            }
            gdata = gdatabuf;
        } else if (Ztls_LIKELY(dst_ghashlen >= 6 * 16)) {
            gdata = dst_ghash;
            gdata_cnt = 6;
            dst_ghash += 6;
            dst_ghashlen -= 96;
        } else {
            gdata_cnt = 0;
        GdataFillDST:
            while (gdata_cnt < 6) {
                if (dst_ghashlen < 16) {
                    if (dst_ghashlen != 0) {
                        gdatabuf[gdata_cnt++] = loadn128(dst_ghash, dst_ghashlen);
                        dst_ghashlen = 0;
                    }
                    if (gdata_cnt < 6)
                        goto Finish;
                    break;
                }
                gdatabuf[gdata_cnt++] = _mm_loadu_si128(dst_ghash++);
                dst_ghashlen -= 16;
            }
            gdata = gdatabuf;
        }
    }

Finish:
    gdatabuf[gdata_cnt++] = ac;

    /* We have complete set of data to be fed into GHASH. Let's finish the remaining calculation.
     * Note that by now, all AES operations for payload encryption and ek0 are complete. This is is because it is necessary for GCM
     * to process at least the same amount of data (i.e. payload-blocks + AC), and because AES is at least one 96-byte block ahead.
     */
    assert(STATE_EK0_READY());
    for (size_t i = 0; i < gdata_cnt; ++i)
        gfmul_nextstep128(&gstate, gdatabuf[i], --ghash_precompute);

    gfmul_reduce128(&gstate);
    _mm_storeu_si128(dst, gfmul_get_tag128(&gstate, ek0));

    /* Finish the calculation of supplemental vector. Done at the very last, because the sample might cover the GCM tag. */
    if ((state & STATE_SUPP_USED) != 0) {
        size_t i;
        if ((state & STATE_SUPP_IN_PROCESS) == 0) {
            bits4keys = ((struct ctr_context *)supp->ctx)->fusion.keys.m128;
            bits4 = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i *>(supp->input)), bits4keys[0]);
            i = 1;
        } else {
            i = 2;
        }
        do {
            bits4 = _mm_aesenc_si128(bits4, bits4keys[i++]);
        } while (i != ctx->super.ecb.rounds);
        bits4 = _mm_aesenclast_si128(bits4, bits4keys[i]);
        _mm_storeu_si128((__m128i *)supp->output, bits4);
    }

#undef AESECB6_INIT
#undef AESECB6_UPDATE
#undef AESECB6_FINAL
#undef STATE_EK0_BEEN_FOUND
#undef STATE_EK0_READY
#undef STATE_SUPP_IN_PROCESS
}

int fusion_aesgcm_decrypt(fusion_aesgcm_context_t *_ctx, void *output, const void *input, size_t inlen, __m128i ctr,
                               const void *_aad, size_t aadlen, const void *tag)
{
    struct fusion_aesgcm_context128 *ctx = reinterpret_cast<struct fusion_aesgcm_context128 *>(_ctx);
    __m128i ek0 = _mm_setzero_si128(), bits0, bits1 = _mm_setzero_si128(), bits2 = _mm_setzero_si128(), bits3 = _mm_setzero_si128(),
            bits4 = _mm_setzero_si128(), bits5 = _mm_setzero_si128();
    struct fusion_gfmul_state128 gstate = {0};
    __m128i gdatabuf[6];
    __m128i ac = _mm_shuffle_epi8(_mm_set_epi32(0, (int)aadlen * 8, 0, (int)inlen * 8), byteswap128);
    struct fusion_aesgcm_ghash_precompute128 *ghash_precompute = ctx->ghash.data() + (aadlen + 15) / 16 + (inlen + 15) / 16 + 1;

    const __m128i *gdata; // points to the elements fed into GHASH
    size_t gdata_cnt;

    const __m128i *src_ghash = static_cast<const __m128i *>(input), *src_aes = static_cast<const __m128i *>(input), *aad = static_cast<const __m128i *>(_aad);
    __m128i *dst = static_cast<__m128i *>(output);
    size_t nondata_aes_cnt = 0, src_ghashlen = inlen, src_aeslen = inlen;

    /* schedule ek0 and suppkey */
    ctr = _mm_add_epi64(ctr, one8);
    bits0 = _mm_xor_si128(_mm_shuffle_epi8(ctr, byteswap128), ctx->super.ecb.keys.m128[0]);
    ++nondata_aes_cnt;

#define STATE_IS_FIRST_RUN 0x1
#define STATE_GHASH_HAS_MORE 0x2
    int state = STATE_IS_FIRST_RUN | STATE_GHASH_HAS_MORE;

    /* the main loop */
    while (1) {

        /* setup gdata */
        if (Ztls_UNLIKELY(aadlen != 0)) {
            gdata = gdatabuf;
            gdata_cnt = 0;
            while (gdata_cnt < 6) {
                if (aadlen < 16) {
                    if (aadlen != 0) {
                        gdatabuf[gdata_cnt++] = loadn128(aad, aadlen);
                        aadlen = 0;
                        ++nondata_aes_cnt;
                    }
                    goto GdataFillSrc;
                }
                gdatabuf[gdata_cnt++] = _mm_loadu_si128(aad++);
                aadlen -= 16;
                ++nondata_aes_cnt;
            }
        } else if (Ztls_LIKELY(src_ghashlen >= 6 * 16)) {
            gdata = src_ghash;
            gdata_cnt = 6;
            src_ghash += 6;
            src_ghashlen -= 6 * 16;
        } else {
            gdata = gdatabuf;
            gdata_cnt = 0;
        GdataFillSrc:
            while (gdata_cnt < 6) {
                if (src_ghashlen < 16) {
                    if (src_ghashlen != 0) {
                        gdatabuf[gdata_cnt++] = loadn128(src_ghash, src_ghashlen);
                        src_ghash = (__m128i *)((uint8_t *)src_ghash + src_ghashlen);
                        src_ghashlen = 0;
                    }
                    if (gdata_cnt < 6 && (state & STATE_GHASH_HAS_MORE) != 0) {
                        gdatabuf[gdata_cnt++] = ac;
                        state &= ~STATE_GHASH_HAS_MORE;
                    }
                    break;
                }
                gdatabuf[gdata_cnt++] = _mm_loadu_si128(src_ghash++);
                src_ghashlen -= 16;
            }
        }

        /* setup aes bits */
        if (Ztls_LIKELY(nondata_aes_cnt == 0))
            goto InitAllBits;
        switch (nondata_aes_cnt) {
#define INIT_BITS(n, keys)                                                                                                         \
    case n:                                                                                                                        \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits##n = _mm_xor_si128(_mm_shuffle_epi8(ctr, byteswap128), keys.m128[0]);
        InitAllBits:
            INIT_BITS(0, ctx->super.ecb.keys);
            INIT_BITS(1, ctx->super.ecb.keys);
            INIT_BITS(2, ctx->super.ecb.keys);
            INIT_BITS(3, ctx->super.ecb.keys);
            INIT_BITS(4, ctx->super.ecb.keys);
            INIT_BITS(5, ctx->super.ecb.keys);
#undef INIT_BITS
        }

        { /* run aes and ghash */
#define AESECB6_UPDATE(i)                                                                                                          \
    do {                                                                                                                           \
        __m128i k = ctx->super.ecb.keys.m128[i];                                                                                   \
        bits0 = _mm_aesenc_si128(bits0, k);                                                                                        \
        bits1 = _mm_aesenc_si128(bits1, k);                                                                                        \
        bits2 = _mm_aesenc_si128(bits2, k);                                                                                        \
        bits3 = _mm_aesenc_si128(bits3, k);                                                                                        \
        bits4 = _mm_aesenc_si128(bits4, k);                                                                                        \
        bits5 = _mm_aesenc_si128(bits5, k);                                                                                        \
    } while (0)

            size_t aesi;
            for (aesi = 1; aesi <= gdata_cnt; ++aesi) {
                AESECB6_UPDATE(aesi);
                gfmul_nextstep128(&gstate, _mm_loadu_si128(gdata++), --ghash_precompute);
            }
            for (; aesi < ctx->super.ecb.rounds; ++aesi)
                AESECB6_UPDATE(aesi);
            __m128i k = ctx->super.ecb.keys.m128[aesi];
            bits0 = _mm_aesenclast_si128(bits0, k);
            bits1 = _mm_aesenclast_si128(bits1, k);
            bits2 = _mm_aesenclast_si128(bits2, k);
            bits3 = _mm_aesenclast_si128(bits3, k);
            bits4 = _mm_aesenclast_si128(bits4, k);
            bits5 = _mm_aesenclast_si128(bits5, k);

#undef AESECB6_UPDATE
        }

        /* apply aes bits */
        if (Ztls_LIKELY(nondata_aes_cnt == 0 && src_aeslen >= 6 * 16)) {
#define APPLY(i) _mm_storeu_si128(dst + i, _mm_xor_si128(_mm_loadu_si128(src_aes + i), bits##i))
            APPLY(0);
            APPLY(1);
            APPLY(2);
            APPLY(3);
            APPLY(4);
            APPLY(5);
#undef APPLY
            dst += 6;
            src_aes += 6;
            src_aeslen -= 6 * 16;
        } else {
            if ((state & STATE_IS_FIRST_RUN) != 0) {
                ek0 = bits0;
                state &= ~STATE_IS_FIRST_RUN;
            }
            switch (nondata_aes_cnt) {
#define APPLY(i)                                                                                                                   \
    case i:                                                                                                                        \
        if (Ztls_LIKELY(src_aeslen > 16)) {                                                                                        \
            _mm_storeu_si128(dst++, _mm_xor_si128(_mm_loadu_si128(src_aes++), bits##i));                                           \
            src_aeslen -= 16;                                                                                                      \
        } else {                                                                                                                   \
            bits0 = bits##i;                                                                                                       \
            goto Finish;                                                                                                           \
        }
                APPLY(0);
                APPLY(1);
                APPLY(2);
                APPLY(3);
                APPLY(4);
                APPLY(5);
#undef APPLY
            }
            nondata_aes_cnt = 0;
        }
    }

Finish:
    if (src_aeslen == 16) {
        _mm_storeu_si128(dst, _mm_xor_si128(_mm_loadu_si128(src_aes), bits0));
    } else if (src_aeslen != 0) {
        storen128(dst, src_aeslen, _mm_xor_si128(loadn128(src_aes, src_aeslen), bits0));
    }

    assert((state & STATE_IS_FIRST_RUN) == 0);

    /* the only case where AES operation is complete and GHASH is not is when the application of AC is remaining */
    if ((state & STATE_GHASH_HAS_MORE) != 0) {
        assert(ghash_precompute - 1 == ctx->ghash.data());
        gfmul_nextstep128(&gstate, ac, --ghash_precompute);
    }

    gfmul_reduce128(&gstate);
    __m128i calctag = gfmul_get_tag128(&gstate, ek0);

    return _mm_movemask_epi8(_mm_cmpeq_epi8(calctag, _mm_loadu_si128(reinterpret_cast<const __m128i *>(tag)))) == 0xffff;

#undef STATE_IS_FIRST_RUN
#undef STATE_GHASH_HAS_MORE
}

static __m128i expand_key(__m128i key, __m128i temp)
{
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));

    key = _mm_xor_si128(key, temp);

    return key;
}

ZuDerive(Aes128Keys, (ZtArray<__m128i, ZtArrayHeapID<"Ztls.Fusion.AES128Keys">>));
ZuDerive(Aes256Keys, (ZtArray<__m256i, ZtArrayHeapID<"Ztls.Fusion.AES256Keys">>));

void fusion_aesecb_init(fusion_aesecb_context_t *ctx, int is_enc, const void *key, size_t key_size, int aesni256)
{
    assert(is_enc && "decryption is not supported (yet)");

    size_t i = 0;

    switch (key_size) {
    case 16: /* AES128 */
        ctx->rounds = 10;
        break;
    case 32: /* AES256 */
        ctx->rounds = 14;
        break;
    default:
        assert(!"invalid key size; AES128 / AES256 are supported");
        break;
    }
    ctx->aesni256 = aesni256;

    Aes128Keys keys128;
    keys128.length(ctx->rounds + 1);
    /* load and expand 128-bit keys before any AVX2 broadcast */
    keys128[i++] = _mm_loadu_si128((__m128i *)key);
    if (key_size == 32)
        keys128[i++] = _mm_loadu_si128((__m128i *)key + 1);
    while (1) {
#define EXPAND(R)                                                                                                                  \
    {                                                                                                                              \
        keys128[i] =                                                                                                        \
            expand_key(keys128[i - key_size / 16],                                                                          \
                       _mm_shuffle_epi32(_mm_aeskeygenassist_si128(keys128[i - 1], R), _MM_SHUFFLE(3, 3, 3, 3)));           \
        if (i == ctx->rounds)                                                                                                      \
            break;                                                                                                                 \
        ++i;                                                                                                                       \
        if (key_size > 24) {                                                                                                       \
            keys128[i] =                                                                                                    \
                expand_key(keys128[i - key_size / 16],                                                                      \
                           _mm_shuffle_epi32(_mm_aeskeygenassist_si128(keys128[i - 1], R), _MM_SHUFFLE(2, 2, 2, 2)));       \
            ++i;                                                                                                                   \
        }                                                                                                                          \
    }
        EXPAND(0x1);
        EXPAND(0x2);
        EXPAND(0x4);
        EXPAND(0x8);
        EXPAND(0x10);
        EXPAND(0x20);
        EXPAND(0x40);
        EXPAND(0x80);
        EXPAND(0x1b);
        EXPAND(0x36);
#undef EXPAND
    }

    if (!aesni256) {
        ctx->keys.m128 = ZuMv(keys128).release();
        return;
    }
    Aes256Keys keys256;
    keys256.length(ctx->rounds + 1);
    for (unsigned i = 0; i <= ctx->rounds; ++i)
        keys256[i] = _mm256_broadcastsi128_si256(keys128[i]);
    ZuClear(keys128.data(), keys128.length() * sizeof(__m128i));
    ctx->keys.m256 = ZuMv(keys256).release();
}

void fusion_aesecb_dispose(fusion_aesecb_context_t *ctx)
{
    if (ctx->aesni256) {
        ZuClear(ctx->keys.m256, (ctx->rounds + 1) * sizeof(__m256i));
        Aes256Keys::free(ctx->keys.m256);
    } else {
        ZuClear(ctx->keys.m128, (ctx->rounds + 1) * sizeof(__m128i));
        Aes128Keys::free(ctx->keys.m128);
    }
    ZuClear(ctx, sizeof(*ctx));
}

void fusion_aesecb_encrypt(fusion_aesecb_context_t *ctx, void *dst, const void *src)
{
    __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src));
    v = aesecb_encrypt(ctx, v);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(dst), v);
}

/**
 * returns the number of ghash entries that is required to handle an AEAD block of given size
 */
static size_t aesgcm_calc_ghash_cnt(size_t capacity)
{
    // round-up by block size, add to handle worst split of the size between AAD and payload, plus context to hash AC
    return (capacity + 15) / 16 + 2;
}

static void setup_one_ghash_entry(fusion_aesgcm_context_t *ctx)
{
    __m128i *H, *r, *Hprev, H0;

    if (ctx->ecb.aesni256) {
        struct fusion_aesgcm_context256 *ctx256 = reinterpret_cast<struct fusion_aesgcm_context256 *>(ctx);
#define GET_SLOT(i, mem) (&ctx256->ghash[(i) / 2].mem[(i) % 2 == 0])
        H = GET_SLOT(ctx->ghash_cnt, H);
        r = GET_SLOT(ctx->ghash_cnt, r);
        Hprev = ctx->ghash_cnt == 0 ? NULL : GET_SLOT(ctx->ghash_cnt - 1, H);
#undef GET_SLOT
        H0 = ctx256->ghash[0].H[1];
    } else {
        struct fusion_aesgcm_context128 *ctx128 = reinterpret_cast<struct fusion_aesgcm_context128 *>(ctx);
        H = &ctx128->ghash[ctx->ghash_cnt].H;
        r = &ctx128->ghash[ctx->ghash_cnt].r;
        Hprev = ctx->ghash_cnt == 0 ? NULL : &ctx128->ghash[ctx->ghash_cnt - 1].H;
        H0 = ctx128->ghash[0].H;
    }

    if (Hprev != NULL)
        *H = gfmul(*Hprev, H0);

    *r = _mm_shuffle_epi32(*H, 78);
    *r = _mm_xor_si128(*r, *H);

    ++ctx->ghash_cnt;
}

template <typename Array>
static void grow_ghash(Array &ghash, size_t count)
{
    if (count <= ghash.size()) {
        ghash.length(count);
        return;
    }
    Array next(count, ZmGrow(ghash.size(), count));
    if (auto size = ghash.length() * sizeof(*ghash.data())) {
        memcpy(next.data(), ghash.data(), size);
        ZuClear(ghash.data(), size);
    }
    ghash = ZuMv(next);
}

static fusion_aesgcm_context_t *new_aesgcm(const void *key, size_t key_size, size_t capacity, int aesni256)
{
    fusion_aesgcm_context_t *ctx;
    if (aesni256) {
        auto storage = new (FusionGcmHeap::alloc(sizeof(fusion_aesgcm_context256))) fusion_aesgcm_context256{};
        ctx = &storage->super;
    } else {
        auto storage = new (FusionGcmHeap::alloc(sizeof(fusion_aesgcm_context128))) fusion_aesgcm_context128{};
        ctx = &storage->super;
    }
    fusion_aesecb_init(&ctx->ecb, 1, key, key_size, aesni256);

    size_t count = aesgcm_calc_ghash_cnt(capacity);
    __m128i H0 = aesecb_encrypt(&ctx->ecb, _mm_setzero_si128());
    H0 = transformH(_mm_shuffle_epi8(H0, byteswap128));
    if (aesni256) {
        auto &ghash = reinterpret_cast<fusion_aesgcm_context256 *>(ctx)->ghash;
        count += count % 2;
        grow_ghash(ghash, count / 2);
        ghash[0].H[1] = H0;
    } else {
        auto &ghash = reinterpret_cast<fusion_aesgcm_context128 *>(ctx)->ghash;
        grow_ghash(ghash, count);
        ghash[0].H = H0;
    }
    ctx->capacity = capacity;
    while (ctx->ghash_cnt < count)
        setup_one_ghash_entry(ctx);
    return ctx;
}

fusion_aesgcm_context_t *fusion_aesgcm_new(const void *key, size_t key_size, size_t capacity)
{
    return new_aesgcm(key, key_size, capacity, 0);
}

fusion_aesgcm_context_t *fusion_aesgcm_set_capacity(fusion_aesgcm_context_t *ctx, size_t capacity)
{
    size_t count = aesgcm_calc_ghash_cnt(capacity);
    if (count <= ctx->ghash_cnt)
        return ctx;
    if (ctx->ecb.aesni256) {
        count += count % 2;
        grow_ghash(reinterpret_cast<fusion_aesgcm_context256 *>(ctx)->ghash, count / 2);
    } else {
        grow_ghash(reinterpret_cast<fusion_aesgcm_context128 *>(ctx)->ghash, count);
    }
    ctx->capacity = capacity;
    while (ctx->ghash_cnt < count)
        setup_one_ghash_entry(ctx);
    return ctx;
}

template <typename Storage>
static void free_gcm(fusion_aesgcm_context_t *ctx)
{
    auto storage = reinterpret_cast<Storage *>(ctx);
    ZuClear(storage->ghash.data(), storage->ghash.length() * sizeof(*storage->ghash.data()));
    fusion_aesecb_dispose(&ctx->ecb);
    storage->~Storage();
    FusionGcmHeap::free(storage, sizeof(Storage));
}

void fusion_aesgcm_free(fusion_aesgcm_context_t *ctx)
{
    if (ctx->ecb.aesni256)
        free_gcm<fusion_aesgcm_context256>(ctx);
    else
        free_gcm<fusion_aesgcm_context128>(ctx);
}

static void ctr_dispose(cipher_context_t *_ctx)
{
    struct ctr_context *ctx = (struct ctr_context *)_ctx;
    fusion_aesecb_dispose(&ctx->fusion);
    _mm_storeu_si128(&ctx->bits, _mm_setzero_si128());
}

static void ctr_init(cipher_context_t *_ctx, const void *iv)
{
    struct ctr_context *ctx = (struct ctr_context *)_ctx;
    _mm_storeu_si128(&ctx->bits, aesecb_encrypt(&ctx->fusion, _mm_loadu_si128(reinterpret_cast<const __m128i *>(iv))));
    ctx->is_ready = 1;
}

static void ctr_transform(cipher_context_t *_ctx, void *output, const void *input, size_t len)
{
    struct ctr_context *ctx = (struct ctr_context *)_ctx;

    assert(ctx->is_ready && len <= 16 &&
           "CTR transfomation is supported only once per call to `init` and the maximum size is limited to 16 bytes");
    ctx->is_ready = 0;

    if (len < 16) {
        storen128(output, len, _mm_xor_si128(_mm_loadu_si128(&ctx->bits), loadn128(input, len)));
    } else {
        _mm_storeu_si128(reinterpret_cast<__m128i *>(output), _mm_xor_si128(_mm_loadu_si128(&ctx->bits), _mm_loadu_si128(reinterpret_cast<const __m128i *>(input))));
    }
}

static int aesctr_setup(cipher_context_t *_ctx, int is_enc, const void *key, size_t key_size)
{
    struct ctr_context *ctx = (struct ctr_context *)_ctx;

    ctx->super.do_dispose = ctr_dispose;
    ctx->super.do_init = ctr_init;
    ctx->super.do_transform = ctr_transform;
    fusion_aesecb_init(&ctx->fusion, 1, key, key_size, 0 /* probably we do not need aesni256 for CTR? */);
    ctx->is_ready = 0;

    return 0;
}

static int aes128ctr_setup(cipher_context_t *ctx, int is_enc, const void *key)
{
    return aesctr_setup(ctx, is_enc, key, Ztls_AES128_KEY_SIZE);
}

static int aes256ctr_setup(cipher_context_t *ctx, int is_enc, const void *key)
{
    return aesctr_setup(ctx, is_enc, key, Ztls_AES256_KEY_SIZE);
}

static void aesgcm_dispose_crypto(aead_context_t *_ctx)
{
    struct aesgcm_context *ctx = (struct aesgcm_context *)_ctx;

    ZuClear(&ctx->serial, sizeof(ctx->serial));
    fusion_aesgcm_free(ctx->aesgcm);
    ctx->aesgcm = NULL;
}

static void aesgcm_ensure_capacity(struct aesgcm_context *ctx, size_t textlen, size_t aadlen)
{
    if (aadlen > SIZE_MAX - textlen)
        abort();

    size_t required = textlen + aadlen;
    if (required > ctx->aesgcm->capacity) {
        fusion_aesgcm_set_capacity(ctx->aesgcm, required);
    }
}

static void aead_do_encrypt_init(aead_context_t *_ctx, uint64_t seq, const void *aad, size_t aadlen)
{
    assert(!"FIXME");
}

static size_t aead_do_encrypt_update(aead_context_t *_ctx, void *output, const void *input, size_t inlen)
{
    assert(!"FIXME");
    return SIZE_MAX;
}

static size_t aead_do_encrypt_final(aead_context_t *_ctx, void *_output)
{
    assert(!"FIXME");
    return SIZE_MAX;
}

static inline __m128i calc_counter(struct aesgcm_context *ctx, uint64_t seq)
{
    __m128i ctr = _mm_setzero_si128();
    ctr = _mm_insert_epi64(ctr, seq, 0);
    ctr = _mm_slli_si128(ctr, 4);
    ctr = _mm_xor_si128(ctx->static_iv, ctr);
    return ctr;
}

/* The serial path never allocates or reads outside the supplied destination span. */
static void serial_hash(struct aesgcm_context *ctx, __m128i block)
{
    __m128i h = ctx->aesgcm->ecb.aesni256
                    ? ((struct fusion_aesgcm_context256 *)ctx->aesgcm)->ghash[0].H[1]
                    : ((struct fusion_aesgcm_context128 *)ctx->aesgcm)->ghash[0].H;
    ctx->serial.hash = gfmul(_mm_xor_si128(ctx->serial.hash, _mm_shuffle_epi8(block, byteswap128)), h);
}

static int serial_error(struct aesgcm_context *ctx)
{
    ZuClear(&ctx->serial, sizeof(ctx->serial));
    return Ztls_ERROR_LIBRARY;
}

int fusion_aead_enc_begin(aead_context_t *_ctx, uint64_t seq, const void *_aad, size_t aadlen)
{
    struct aesgcm_context *ctx = reinterpret_cast<struct aesgcm_context *>(_ctx);
    const uint8_t *aad = static_cast<const uint8_t *>(_aad);
    ZuClear(&ctx->serial, sizeof(ctx->serial));
    if (aadlen > UINT64_MAX / 8 || (aadlen != 0 && aad == NULL))
        return serial_error(ctx);
    ctx->serial.aadlen = aadlen;
    ctx->serial.ctr = _mm_insert_epi32(calc_counter(ctx, seq), 1, 0);
    ctx->serial.j0 = _mm_shuffle_epi8(ctx->serial.ctr, byteswap128);
    ctx->serial.bit_used = 16;
    ctx->serial.active = 1;
    while (aadlen >= 16) {
        serial_hash(ctx, _mm_loadu_si128(reinterpret_cast<const __m128i *>((const void *)aad)));
        aad += 16;
        aadlen -= 16;
    }
    if (aadlen != 0) {
        uint8_t block[16] = {0};
        memcpy(block, aad, aadlen);
        serial_hash(ctx, _mm_loadu_si128(reinterpret_cast<const __m128i *>((const void *)block)));
    }
    return 0;
}

/* Full-block kernel keeps six AES pipelines in flight, using the selected expanded keys. */
static void serial_blocks(struct aesgcm_context *ctx, uint8_t *dst, const uint8_t *src, int wide, int decrypt)
{
    fusion_aesecb_context_t *ecb = &ctx->aesgcm->ecb;
    if (wide) {
        struct fusion_aesgcm_context256 *gctx = reinterpret_cast<struct fusion_aesgcm_context256 *>(ctx->aesgcm);
        struct fusion_gfmul_state256 gstate = {.lo = _mm256_zextsi128_si256(ctx->serial.hash)};
        __m256i bits[6];
        for (size_t i = 0; i != 6; ++i) {
            ctx->serial.ctr = _mm_add_epi32(ctx->serial.ctr, _mm_set_epi32(0, 0, 0, 1));
            __m128i first = _mm_shuffle_epi8(ctx->serial.ctr, byteswap128);
            ctx->serial.ctr = _mm_add_epi32(ctx->serial.ctr, _mm_set_epi32(0, 0, 0, 1));
            bits[i] = _mm256_inserti128_si256(_mm256_castsi128_si256(first),
                                            _mm_shuffle_epi8(ctx->serial.ctr, byteswap128), 1);
            bits[i] = _mm256_xor_si256(bits[i], ecb->keys.m256[0]);
        }
        for (size_t r = 1; r < ecb->rounds; ++r)
            for (size_t i = 0; i != 6; ++i)
                bits[i] = _mm256_aesenc_epi128(bits[i], ecb->keys.m256[r]);
        for (size_t i = 0; i != 6; ++i) {
            __m256i enc = _mm256_xor_si256(_mm256_aesenclast_epi128(bits[i], ecb->keys.m256[ecb->rounds]),
                                         _mm256_loadu_si256(reinterpret_cast<const __m256i *>((const void *)(src + 32 * i))));
            __m256i auth = decrypt ? _mm256_loadu_si256(reinterpret_cast<const __m256i *>((const void *)(src + 32 * i))) : enc;
            if (ctx->super.algo->non_temporal && ((uintptr_t)(dst + 32 * i) & 31) == 0)
                _mm256_stream_si256((void *)(dst + 32 * i), enc);
            else
                _mm256_storeu_si256(reinterpret_cast<__m256i *>((void *)(dst + 32 * i)), enc);
            if (i == 0)
                gfmul_firststep256(&gstate, auth, 0, gctx->ghash.data() + 5);
            else
                gfmul_nextstep256(&gstate, auth, gctx->ghash.data() + 5 - i);
        }
        gfmul_reduce256(&gstate);
        ctx->serial.hash = _mm256_castsi256_si128(gstate.lo);
    } else {
        struct fusion_aesgcm_context128 *gctx = reinterpret_cast<struct fusion_aesgcm_context128 *>(ctx->aesgcm);
        struct fusion_gfmul_state128 gstate = {.lo = ctx->serial.hash};
        __m128i bits[6];
        for (size_t i = 0; i != 6; ++i) {
            ctx->serial.ctr = _mm_add_epi32(ctx->serial.ctr, _mm_set_epi32(0, 0, 0, 1));
            bits[i] = _mm_xor_si128(_mm_shuffle_epi8(ctx->serial.ctr, byteswap128), ecb->keys.m128[0]);
        }
        for (size_t r = 1; r < ecb->rounds; ++r)
            for (size_t i = 0; i != 6; ++i)
                bits[i] = _mm_aesenc_si128(bits[i], ecb->keys.m128[r]);
        for (size_t i = 0; i != 6; ++i) {
            __m128i enc = _mm_xor_si128(_mm_aesenclast_si128(bits[i], ecb->keys.m128[ecb->rounds]),
                                      _mm_loadu_si128(reinterpret_cast<const __m128i *>((const void *)(src + 16 * i))));
            __m128i auth = decrypt ? _mm_loadu_si128(reinterpret_cast<const __m128i *>((const void *)(src + 16 * i))) : enc;
            if (ctx->super.algo->non_temporal && ((uintptr_t)(dst + 16 * i) & 15) == 0)
                _mm_stream_si128((void *)(dst + 16 * i), enc);
            else
                _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)(dst + 16 * i)), enc);
            if (i == 0)
                gfmul_firststep128(&gstate, auth, gctx->ghash.data() + 5);
            else
                gfmul_nextstep128(&gstate, auth, gctx->ghash.data() + 5 - i);
        }
        gfmul_reduce128(&gstate);
        ctx->serial.hash = gstate.lo;
    }
}

static int serial_chunk(aead_context_t *_ctx, void *_output, const void *_input, size_t length, size_t *written,
                        int decrypt)
{
    struct aesgcm_context *ctx = reinterpret_cast<struct aesgcm_context *>(_ctx);
    uint8_t *dst = static_cast<uint8_t *>(_output);
    const uint8_t *src = static_cast<const uint8_t *>(_input);
    *written = 0;
    if (!ctx->serial.active || length > UINT64_MAX / 8 - ctx->serial.textlen ||
        (length != 0 && (dst == NULL || src == NULL)))
        return serial_error(ctx);
    ctx->serial.textlen += length;
    size_t left = length, batch = ctx->aesgcm->ecb.aesni256 ? 192 : 96;
    while (left != 0) {
        if (ctx->serial.bit_used == 16 && left >= batch) {
            serial_blocks(ctx, dst, src, ctx->aesgcm->ecb.aesni256, decrypt);
            dst += batch;
            src += batch;
            left -= batch;
            continue;
        }
        if (ctx->serial.bit_used == 16) {
            ctx->serial.ctr = _mm_add_epi32(ctx->serial.ctr, _mm_set_epi32(0, 0, 0, 1));
            _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)ctx->serial.bits),
                            aesecb_encrypt(&ctx->aesgcm->ecb, _mm_shuffle_epi8(ctx->serial.ctr, byteswap128)));
            ctx->serial.bit_used = 0;
        }
        size_t n = 16 - ctx->serial.bit_used;
        if (n > left)
            n = left;
        for (size_t i = 0; i != n; ++i) {
            uint8_t c = src[i];
            uint8_t p = c ^ ctx->serial.bits[ctx->serial.bit_used++];
            dst[i] = p;
            ctx->serial.carry[ctx->serial.carry_used++] = decrypt ? c : p;
        }
        if (ctx->serial.carry_used == 16) {
            serial_hash(ctx, _mm_loadu_si128(reinterpret_cast<const __m128i *>((const void *)ctx->serial.carry)));
            ctx->serial.carry_used = 0;
        }
        dst += n;
        src += n;
        left -= n;
    }
    if (ctx->super.algo->non_temporal)
        _mm_sfence();
    *written = length;
    return 0;
}

int fusion_aead_enc_chunk(aead_context_t *ctx, void *output, const void *input, size_t length, size_t *written)
{
    return serial_chunk(ctx, output, input, length, written, 0);
}

int fusion_aead_dec_chunk(aead_context_t *ctx, void *output, size_t capacity,
                              const void *input, size_t length, size_t *written)
{
    *written = 0;
    if (length > capacity)
        return serial_error(reinterpret_cast<struct aesgcm_context *>((void *)ctx));
    return serial_chunk(ctx, output, input, length, written, 1);
}

int fusion_aead_enc_end(aead_context_t *_ctx, void *output, size_t capacity, size_t *written)
{
    struct aesgcm_context *ctx = reinterpret_cast<struct aesgcm_context *>(_ctx);
    *written = 0;
    if (!ctx->serial.active || capacity < 16 || output == NULL)
        return serial_error(ctx);
    if (ctx->serial.carry_used != 0) {
        memset(ctx->serial.carry + ctx->serial.carry_used, 0, 16 - ctx->serial.carry_used);
        serial_hash(ctx, _mm_loadu_si128(reinterpret_cast<const __m128i *>((const void *)ctx->serial.carry)));
    }
    __m128i lengths = _mm_set_epi64x(ctx->serial.aadlen * 8, ctx->serial.textlen * 8);
    serial_hash(ctx, _mm_shuffle_epi8(lengths, byteswap128));
    _mm_storeu_si128(reinterpret_cast<__m128i *>(output), _mm_xor_si128(_mm_shuffle_epi8(ctx->serial.hash, byteswap128),
                                         aesecb_encrypt(&ctx->aesgcm->ecb, ctx->serial.j0)));
    ZuClear(&ctx->serial, sizeof(ctx->serial));
    *written = 16;
    return 0;
}

int fusion_aead_dec_begin(aead_context_t *ctx, uint64_t seq, const void *aad, size_t aadlen)
{
    return fusion_aead_enc_begin(ctx, seq, aad, aadlen);
}

int fusion_aead_dec_end(aead_context_t *ctx, const void *tag, size_t taglen)
{
    uint8_t expected[16];
    size_t written;
    if (tag == NULL || taglen != sizeof(expected))
        return serial_error(reinterpret_cast<struct aesgcm_context *>((void *)ctx));
    int ret = fusion_aead_enc_end(ctx, expected, sizeof(expected), &written);
    if (ret == 0 && !Ztls::ctEqual({expected, sizeof(expected)}, {static_cast<const uint8_t *>(tag), sizeof(expected)}))
        ret = Ztls_ALERT_BAD_RECORD_MAC;
    ZuClear(expected, sizeof(expected));
    return ret;
}

void fusion_aead_dec_abort(aead_context_t *ctx)
{
    serial_error(reinterpret_cast<struct aesgcm_context *>((void *)ctx));
}

void aead_do_encrypt(struct st_aead_context_t *_ctx, void *output, const void *input, size_t inlen, uint64_t seq,
                     const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp)
{
    struct aesgcm_context *ctx = reinterpret_cast<struct aesgcm_context *>(_ctx);

    aesgcm_ensure_capacity(ctx, inlen, aadlen);
    fusion_aesgcm_encrypt(ctx->aesgcm, output, input, inlen, calc_counter(ctx, seq), aad, aadlen, supp);
}

ZuDerive(FusionOverlap, (ZtBArray<ZtArrayHeapID<"Ztls.Fusion.OverlapScratch">>));

static void aead_do_encrypt_v(struct st_aead_context_t *_ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq,
                              const void *aad, size_t aadlen)
{
    size_t inlen = aead__sum_iovec(input, incnt);
    int use_scratch = 0;

    if (incnt == 1) {
        aead_do_encrypt(_ctx, output, input[0].data(), input[0].length(), seq, aad, aadlen, NULL);
        return;
    }

    size_t off = 0;
    for (size_t i = 0; i != incnt; ++i) {
        uintptr_t out_start = (uintptr_t)output, out_end = out_start + inlen;
        uintptr_t in_start = (uintptr_t)input[i].data(), in_end = in_start + input[i].length();

        if (input[i].length() != 0 && in_start < out_end && out_start < in_end && input[i].data() != (uint8_t *)output + off) {
            use_scratch = 1;
            break;
        }
        off += input[i].length();
    }

    auto encrypt = [_ctx, output, input, incnt, inlen, seq, aad, aadlen](uint8_t *dst) {
        size_t offset = 0;
        for (size_t i = 0; i != incnt; ++i) {
            if (input[i].length() != 0)
                memmove(dst + offset, input[i].data(), input[i].length());
            offset += input[i].length();
        }
        aead_do_encrypt(_ctx, output, dst, inlen, seq, aad, aadlen, NULL);
    };

    if (!use_scratch) {
        encrypt(static_cast<uint8_t *>(output));
        return;
    }

    // Ordinary TLS records fit the scratch API. Keep the original wide length
    // for direct backend calls with exceptionally large vectors.
    unsigned size = inlen <= UINT_MAX ? unsigned(inlen) : 0;
    auto scratch = ZtScratch(FusionOverlap, size, size);
    if (!size)
        scratch.length(inlen);
    ZuGuard clear{[&scratch]() { ZuClear(scratch.data(), scratch.length()); }};
    encrypt(scratch.data());
}

static void aead_do_encrypt_v_s(struct st_aead_context_t *_ctx, void *output, const Bytes *input, size_t incnt,
                                uint64_t seq, const void *aad, size_t aadlen, aead_supplementary_encryption_t *supp)
{
    struct aesgcm_context *ctx = (struct aesgcm_context *)_ctx;
    size_t inlen = aead__sum_iovec(input, incnt);
    struct fusion_input input_iter = fusion_input_iovec(input, incnt);

    aesgcm_ensure_capacity(ctx, inlen, aadlen);

    fusion_aesgcm_encrypt_iter(ctx->aesgcm, output, &input_iter, inlen, calc_counter(ctx, seq), aad, aadlen);
    aead__do_supplementary(supp);
}

static size_t aead_do_decrypt(aead_context_t *_ctx, void *output, const void *input, size_t inlen, uint64_t seq,
                              const void *aad, size_t aadlen)
{
    struct aesgcm_context *ctx = reinterpret_cast<struct aesgcm_context *>(_ctx);

    if (inlen < 16)
        return SIZE_MAX;

    size_t enclen = inlen - 16;
    aesgcm_ensure_capacity(ctx, enclen, aadlen);
    if (!fusion_aesgcm_decrypt(ctx->aesgcm, output, input, enclen, calc_counter(ctx, seq), aad, aadlen,
                                    (const uint8_t *)input + enclen))
        return SIZE_MAX;
    return enclen;
}

static inline void aesgcm_get_iv(aead_context_t *_ctx, void *iv)
{
    struct aesgcm_context *ctx = (struct aesgcm_context *)_ctx;

    __m128i m128 = _mm_shuffle_epi8(ctx->static_iv, byteswap128);
    storen128(iv, Ztls_AESGCM_IV_SIZE, m128);
}

static inline void aesgcm_set_iv(aead_context_t *_ctx, const void *iv)
{
    struct aesgcm_context *ctx = (struct aesgcm_context *)_ctx;

    ctx->static_iv = loadn128(iv, Ztls_AESGCM_IV_SIZE);
    ctx->static_iv = _mm_shuffle_epi8(ctx->static_iv, byteswap128);
}

static int aesgcm_setup(aead_context_t *_ctx, int is_enc, const void *key, const void *iv, size_t key_size)
{
    struct aesgcm_context *ctx = (struct aesgcm_context *)_ctx;

    memset(&ctx->serial, 0, sizeof(ctx->serial));
    ctx->super.do_enc_begin = is_enc ? fusion_aead_enc_begin : NULL;
    ctx->super.do_enc_chunk = is_enc ? fusion_aead_enc_chunk : NULL;
    ctx->super.do_enc_end = is_enc ? fusion_aead_enc_end : NULL;
    ctx->super.do_dec_begin = is_enc ? NULL : fusion_aead_dec_begin;
    ctx->super.do_dec_chunk = is_enc ? NULL : fusion_aead_dec_chunk;
    ctx->super.do_dec_end = is_enc ? NULL : fusion_aead_dec_end;
    ctx->super.do_dec_abort = is_enc ? NULL : fusion_aead_dec_abort;
    ctx->static_iv = loadn128(iv, Ztls_AESGCM_IV_SIZE);
    ctx->static_iv = _mm_shuffle_epi8(ctx->static_iv, byteswap128);
    if (key == NULL)
        return 0;

    ctx->super.dispose_crypto = aesgcm_dispose_crypto;
    ctx->super.do_get_iv = aesgcm_get_iv;
    ctx->super.do_set_iv = aesgcm_set_iv;
    ctx->super.do_encrypt_init = aead_do_encrypt_init;
    ctx->super.do_encrypt_update = aead_do_encrypt_update;
    ctx->super.do_encrypt_final = aead_do_encrypt_final;
    ctx->super.do_encrypt = aead_do_encrypt;
    ctx->super.do_encrypt_v = aead_do_encrypt_v;
    ctx->super.do_encrypt_v_s = is_enc ? aead_do_encrypt_v_s : NULL;
    ctx->super.do_decrypt = aead_do_decrypt;

    ctx->aesgcm = new_aesgcm(key, key_size, 1500 /* assume ordinary packet size */, 0 /* no support for aesni256 yet */);
    return 0;
}

static int aes128gcm_setup(aead_context_t *ctx, int is_enc, const void *key, const void *iv)
{
    return aesgcm_setup(ctx, is_enc, key, iv, Ztls_AES128_KEY_SIZE);
}

static int aes256gcm_setup(aead_context_t *ctx, int is_enc, const void *key, const void *iv)
{
    return aesgcm_setup(ctx, is_enc, key, iv, Ztls_AES256_KEY_SIZE);
}

int fusion_can_aesni256 = 0;
cipher_algorithm_t fusion_aes128ctr = {"AES128-CTR",
                                                 Ztls_AES128_KEY_SIZE,
                                                 1, // block size
                                                 Ztls_AES_IV_SIZE,
                                                 sizeof(struct ctr_context),
                                                 aes128ctr_setup};
cipher_algorithm_t fusion_aes256ctr = {"AES256-CTR",
                                                 Ztls_AES256_KEY_SIZE,
                                                 1, // block size
                                                 Ztls_AES_IV_SIZE,
                                                 sizeof(struct ctr_context),
                                                 aes256ctr_setup};
aead_algorithm_t fusion_aes128gcm = {"AES128-GCM",
                                               Ztls_AESGCM_CONFIDENTIALITY_LIMIT,
                                               Ztls_AESGCM_INTEGRITY_LIMIT,
                                               &fusion_aes128ctr,
                                               NULL, // &fusion_aes128ecb,
                                               Ztls_AES128_KEY_SIZE,
                                               Ztls_AESGCM_IV_SIZE,
                                               Ztls_AESGCM_TAG_SIZE,
                                               0,
                                               0,
                                               sizeof(struct aesgcm_context),
                                               aes128gcm_setup,
                                                1,
                                                1};
aead_algorithm_t fusion_aes256gcm = {"AES256-GCM",
                                               Ztls_AESGCM_CONFIDENTIALITY_LIMIT,
                                               Ztls_AESGCM_INTEGRITY_LIMIT,
                                               &fusion_aes256ctr,
                                               NULL, // &fusion_aes256ecb,
                                               Ztls_AES256_KEY_SIZE,
                                               Ztls_AESGCM_IV_SIZE,
                                               Ztls_AESGCM_TAG_SIZE,
                                               0,
                                               0,
                                               sizeof(struct aesgcm_context),
                                               aes256gcm_setup,
                                                1,
                                                1};

static inline void reduce_aad128(struct fusion_gfmul_state128 *gstate, struct fusion_aesgcm_ghash_precompute128 *ghash,
                                 const void *_aad, size_t aadlen)
{
    struct fusion_aesgcm_ghash_precompute128 *ghash_precompute;
    const uint8_t *aad = static_cast<const uint8_t *>(_aad);

    while (Ztls_UNLIKELY(aadlen >= 6 * 16)) {
        ghash_precompute = ghash + 6;
        gfmul_firststep128(gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)aad)), --ghash_precompute);
        aad += 16;
        aadlen -= 16;
        for (int i = 1; i < 6; ++i) {
            gfmul_nextstep128(gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)aad)), --ghash_precompute);
            aad += 16;
            aadlen -= 16;
        }
        gfmul_reduce128(gstate);
    }

    if (Ztls_LIKELY(aadlen != 0)) {
        ghash_precompute = ghash + (aadlen + 15) / 16;
        if (Ztls_UNLIKELY(aadlen >= 16)) {
            gfmul_firststep128(gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)aad)), --ghash_precompute);
            aad += 16;
            aadlen -= 16;
            while (aadlen >= 16) {
                gfmul_nextstep128(gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)aad)), --ghash_precompute);
                aad += 16;
                aadlen -= 16;
            }
            if (Ztls_LIKELY(aadlen != 0))
                gfmul_nextstep128(gstate, loadn128(aad, aadlen), --ghash_precompute);
        } else {
            gfmul_firststep128(gstate, loadn128(aad, aadlen), --ghash_precompute);
        }
        assert(ghash == ghash_precompute);
        gfmul_reduce128(gstate);
    }
}

NO_SANITIZE_ADDRESS
static inline uint8_t *load_preceding_unaligned(uint8_t *encbuf, uint8_t **output)
{
    uint8_t *encp;

    if ((encp = encbuf + ((uintptr_t)*output & 63)) != encbuf) {
        _mm256_store_si256(reinterpret_cast<__m256i *>((void *)encbuf), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(*output - (encp - encbuf)))));
        _mm256_store_si256(reinterpret_cast<__m256i *>((void *)(encbuf + 32)), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(*output - (encp - encbuf) + 32))));
        *output -= encp - encbuf;
    }

    return encp;
}

NO_SANITIZE_ADDRESS
static inline void write_remaining_bytes(uint8_t *dst, const uint8_t *src, const uint8_t *end)
{
    /* Write in 64-byte chunks, using NT store instructions. Last partial block, if any, is written to cache, as that cache line
     * would likely be read when the next TLS record is being built. */

    for (; end - src >= 64; dst += 64, src += 64) {
        _mm256_stream_si256((void *)dst, _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)src)));
        _mm256_stream_si256((void *)(dst + 32), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(src + 32))));
    }
    _mm_sfence(); /* weakly ordered writes have to be synced before being passed to NIC */
    if (src != end) {
        for (; end - src >= 16; dst += 16, src += 16)
            _mm_store_si128(reinterpret_cast<__m128i *>((void *)dst), _mm_load_si128(reinterpret_cast<const __m128i *>((void *)src)));
        if (src != end)
            storen128((void *)dst, end - src, loadn128((void *)src, end - src));
    }
}

NO_SANITIZE_ADDRESS
static void non_temporal_encrypt_v128(struct st_aead_context_t *_ctx, void *_output, const Bytes *input, size_t incnt,
                                      uint64_t seq, const void *aad, size_t aadlen)
{
/* init the bits (we can always run in full), but use the last slot for calculating ek0, if possible */
#define AESECB6_INIT()                                                                                                             \
    do {                                                                                                                           \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits0 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits1 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits2 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits3 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits4 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        if (Ztls_LIKELY(srclen > 16 * 5) || src_vecleft != 0) {                                                                    \
            ctr = _mm_add_epi64(ctr, one8);                                                                                        \
            bits5 = _mm_shuffle_epi8(ctr, byteswap128);                                                                            \
        } else {                                                                                                                   \
            bits5 = ek0;                                                                                                           \
            state |= STATE_EK0_READY;                                                                                              \
        }                                                                                                                          \
        __m128i k = ctx->super.ecb.keys.m128[0];                                                                                   \
        bits0 = _mm_xor_si128(bits0, k);                                                                                           \
        bits1 = _mm_xor_si128(bits1, k);                                                                                           \
        bits2 = _mm_xor_si128(bits2, k);                                                                                           \
        bits3 = _mm_xor_si128(bits3, k);                                                                                           \
        bits4 = _mm_xor_si128(bits4, k);                                                                                           \
        bits5 = _mm_xor_si128(bits5, k);                                                                                           \
    } while (0)

/* aes block update */
#define AESECB6_UPDATE(i)                                                                                                          \
    do {                                                                                                                           \
        __m128i k = ctx->super.ecb.keys.m128[i];                                                                                   \
        bits0 = _mm_aesenc_si128(bits0, k);                                                                                        \
        bits1 = _mm_aesenc_si128(bits1, k);                                                                                        \
        bits2 = _mm_aesenc_si128(bits2, k);                                                                                        \
        bits3 = _mm_aesenc_si128(bits3, k);                                                                                        \
        bits4 = _mm_aesenc_si128(bits4, k);                                                                                        \
        bits5 = _mm_aesenc_si128(bits5, k);                                                                                        \
    } while (0)

/* aesenclast */
#define AESECB6_FINAL(i)                                                                                                           \
    do {                                                                                                                           \
        __m128i k = ctx->super.ecb.keys.m128[i];                                                                                   \
        bits0 = _mm_aesenclast_si128(bits0, k);                                                                                    \
        bits1 = _mm_aesenclast_si128(bits1, k);                                                                                    \
        bits2 = _mm_aesenclast_si128(bits2, k);                                                                                    \
        bits3 = _mm_aesenclast_si128(bits3, k);                                                                                    \
        bits4 = _mm_aesenclast_si128(bits4, k);                                                                                    \
        bits5 = _mm_aesenclast_si128(bits5, k);                                                                                    \
    } while (0)

    struct aesgcm_context *agctx = reinterpret_cast<struct aesgcm_context *>(_ctx);
    uint8_t *output = static_cast<uint8_t *>(_output);

#define STATE_EK0_READY 0x1
#define STATE_COPY_128B 0x2
    int32_t state = 0;

    /* Bytes are written here first then written using NT store instructions, 64 bytes at a time. */
    uint8_t encbuf[32 * 6] __attribute__((aligned(32))), *encp;

    /* `encbuf` should be large enough to store up to 63-bytes of unaligned bytes, 6 16-byte AES blocks, plus AEAD tag that is
     * append to the ciphertext before writing the bytes to main memory using NT store instructions. */
    Ztls_BUILD_ASSERT(sizeof(encbuf) >= 64 + 6 * 16 + 16);

    /* load unaligned data within same cache line preceding `output`, adjusting pointers accordingly */
    encp = load_preceding_unaligned(encbuf, &output);

    /* First write would be 128 bytes (32+6*16), if encbuf contains no less than 32 bytes already. */
    if (encp - encbuf >= 32)
        state |= STATE_COPY_128B;

    /* setup ctr, retain Ek(0), len(A) | len(C) to be fed into GCM */
    size_t total_length = aead__sum_iovec(input, incnt);
    __m128i ctr = calc_counter(agctx, seq);
    ctr = _mm_insert_epi32(ctr, 1, 0);
    __m128i ek0 = _mm_shuffle_epi8(ctr, byteswap128);
    __m128i ac = _mm_shuffle_epi8(_mm_set_epi32(0, (int)aadlen * 8, 0, (int)total_length * 8), byteswap128);

    struct fusion_aesgcm_context128 *ctx = reinterpret_cast<struct fusion_aesgcm_context128 *>(agctx->aesgcm);
    __m128i bits0, bits1, bits2, bits3, bits4, bits5 = _mm_setzero_si128();
    struct fusion_gfmul_state128 gstate = {0};

    /* find the first non-empty vec */
    const uint8_t *src = NULL;
    size_t srclen = 0, src_vecleft = incnt;
    while (srclen == 0 && src_vecleft != 0) {
        src = reinterpret_cast<const uint8_t *>(input[0].data());
        srclen = input[0].length();
        ++input;
        --src_vecleft;
    }

    /* Prepare first 6 blocks of bit stream, at the same time calculating ghash of AAD. */
    AESECB6_INIT();
    AESECB6_UPDATE(1);
    AESECB6_UPDATE(2);
    reduce_aad128(&gstate, ctx->ghash.data(), aad, aadlen);
    for (size_t i = 3; i < ctx->super.ecb.rounds; ++i)
        AESECB6_UPDATE(i);
    AESECB6_FINAL(ctx->super.ecb.rounds);

    /* Main loop. This loop:
     *  1. using current keystream (bits0..bits5), xors a up to 6 * 16 bytes and writes to encbuf,
     *  2. then if there is no more data to be encrypted, exit the loop, otherwise,
     *  3. calculate ghash of the blocks being written to encbuf,
     *  4. calculate next 6 * 16 bytes of keystream,
     *  5. writes encbuf in 64-byte blocks
     * When exitting the loop, `remaining_ghash_from` represents the offset within `encbuf` from where ghash remains to be
     * calculated. */
    size_t remaining_ghash_from = encp - encbuf;
    if (srclen != 0) {
        while (1) {
            /* apply the bit stream to input, writing to encbuf */
            if (Ztls_LIKELY(srclen >= 6 * 16)) {
#define APPLY(i) _mm_storeu_si128(reinterpret_cast<__m128i *>(encp + i * 16), _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i * 16)), bits##i))
                APPLY(0);
                APPLY(1);
                APPLY(2);
                APPLY(3);
                APPLY(4);
                APPLY(5);
#undef APPLY
                encp += 6 * 16;
                src += 6 * 16;
                srclen -= 6 * 16;
                if (Ztls_UNLIKELY(srclen == 0)) {
                    if (src_vecleft == 0) {
                        remaining_ghash_from = (encp - encbuf) - 96;
                        break;
                    }
                    src = reinterpret_cast<const uint8_t *>(input[0].data());
                    srclen = input[0].length();
                    ++input;
                    --src_vecleft;
                }
            } else {
                /* slow path, load at most 6 * 16 bytes to encbuf then encrypt in-place */
                size_t bytes_copied = 0;
                do {
                    if (srclen >= 16 && bytes_copied < 5 * 16) {
                        _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)(encp + bytes_copied)), _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)src)));
                        bytes_copied += 16;
                        src += 16;
                        srclen -= 16;
                    } else {
                        encp[bytes_copied++] = *src++;
                        --srclen;
                    }
                    if (Ztls_UNLIKELY(srclen == 0)) {
                        do {
                            if (src_vecleft == 0)
                                break;
                            src = reinterpret_cast<const uint8_t *>(input[0].data());
                            srclen = input[0].length();
                            ++input;
                            --src_vecleft;
                        } while (srclen == 0);
                        if (srclen == 0)
                            break;
                    }
                } while (bytes_copied < 6 * 16);
#define APPLY(i) _mm_storeu_si128(reinterpret_cast<__m128i *>(encp + i * 16), _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i *>(encp + i * 16)), bits##i))
                APPLY(0);
                APPLY(1);
                APPLY(2);
                APPLY(3);
                APPLY(4);
                APPLY(5);
#undef APPLY
                encp += bytes_copied;
                if (Ztls_UNLIKELY(srclen == 0)) {
                    /* Calculate amonut of data left to be ghashed, as well as zero-clearing the remainedr of partial block, as it
                     * will be fed into ghash. */
                    remaining_ghash_from = (encp - encbuf) - bytes_copied;
                    if ((bytes_copied & 15) != 0)
                        _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)encp), _mm_setzero_si128());
                    break;
                }
            }

            /* Next 96-byte block starts here. Run AES and ghash in while writing output using non-temporal stores in 64-byte
             * blocks. */
            AESECB6_INIT();
            struct fusion_aesgcm_ghash_precompute128 *ghash_precompute = ctx->ghash.data() + 6;
            gfmul_firststep128(&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encp - 6 * 16))), --ghash_precompute);
            AESECB6_UPDATE(1);
            gfmul_nextstep128(&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encp - 5 * 16))), --ghash_precompute);
            AESECB6_UPDATE(2);
            gfmul_nextstep128(&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encp - 4 * 16))), --ghash_precompute);
            AESECB6_UPDATE(3);
            _mm256_stream_si256((void *)output, _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)encbuf)));
            _mm256_stream_si256((void *)(output + 32), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 32))));
            AESECB6_UPDATE(4);
            gfmul_nextstep128(&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encp - 3 * 16))), --ghash_precompute);
            AESECB6_UPDATE(5);
            gfmul_nextstep128(&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encp - 2 * 16))), --ghash_precompute);
            AESECB6_UPDATE(6);
            gfmul_nextstep128(&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encp - 1 * 16))), --ghash_precompute);
            AESECB6_UPDATE(7);
            if ((state & STATE_COPY_128B) != 0) {
                _mm256_stream_si256((void *)(output + 64), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 64))));
                _mm256_stream_si256((void *)(output + 96), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 96))));
                output += 128;
                encp -= 128;
                AESECB6_UPDATE(8);
                _mm256_store_si256(reinterpret_cast<__m256i *>((void *)encbuf), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 128))));
                _mm256_store_si256(reinterpret_cast<__m256i *>((void *)(encbuf + 32)), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 160))));
            } else {
                output += 64;
                encp -= 64;
                _mm256_store_si256(reinterpret_cast<__m256i *>((void *)encbuf), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 64))));
                _mm256_store_si256(reinterpret_cast<__m256i *>((void *)(encbuf + 32)), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 96))));
                AESECB6_UPDATE(8);
            }
            state ^= STATE_COPY_128B;
            AESECB6_UPDATE(9);
            if (Ztls_UNLIKELY(ctx->super.ecb.rounds != 10)) {
                for (size_t i = 10; Ztls_LIKELY(i < ctx->super.ecb.rounds); ++i)
                    AESECB6_UPDATE(i);
            }
            assert(ctx->ghash.data() == ghash_precompute);
            gfmul_reduce128(&gstate);
            AESECB6_FINAL(ctx->super.ecb.rounds);
        }
    }

    /* Now, All the encrypted bits are built in encbuf. Calculate AEAD tag and append to encbuf. */

    { /* Run ghash against the remaining bytes, after appending `ac` (i.e., len(A) | len(C)). At this point, we might be ghashing 7
       * blocks at once. */
        size_t ac_off = remaining_ghash_from + ((encp - encbuf) - remaining_ghash_from + 15) / 16 * 16;
        _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)(encbuf + ac_off)), ac);
        size_t blocks = ((encp - encbuf) - remaining_ghash_from + 15) / 16 + 1; /* round up, +1 for AC */
        assert(blocks <= 7);
        struct fusion_aesgcm_ghash_precompute128 *ghash_precompute = ctx->ghash.data() + blocks;
        gfmul_firststep128(&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encbuf + remaining_ghash_from))), --ghash_precompute);
        remaining_ghash_from += 16;
        while (ghash_precompute != ctx->ghash.data()) {
            gfmul_nextstep128(&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encbuf + remaining_ghash_from))), --ghash_precompute);
            remaining_ghash_from += 16;
        }
        gfmul_reduce128(&gstate);
    }

    /* Calculate EK0, if in the unlikely case on not been done yet. When encoding in full size (16K), EK0 will be ready. */
    if (Ztls_UNLIKELY((state & STATE_EK0_READY) == 0)) {
        bits5 = _mm_xor_si128(ek0, ctx->super.ecb.keys.m128[0]);
        for (size_t i = 1; i < ctx->super.ecb.rounds; ++i)
            bits5 = _mm_aesenc_si128(bits5, ctx->super.ecb.keys.m128[i]);
        bits5 = _mm_aesenclast_si128(bits5, ctx->super.ecb.keys.m128[ctx->super.ecb.rounds]);
    }

    /* append tag to encbuf */
    _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)encp), gfmul_get_tag128(&gstate, bits5));
    encp += 16;

    /* write remaining bytes */
    write_remaining_bytes(output, encbuf, encp);

#undef AESECB6_INIT
#undef AESECB6_UPDATE
#undef AESECB6_FINAL
#undef STATE_EK0_READY
#undef STATE_COPY_128B
}

static size_t non_temporal_decrypt128(aead_context_t *_ctx, void *_output, const void *_input, size_t inlen, uint64_t seq,
                                      const void *aad, size_t aadlen)
{
    /* Bail out if the input is too short, or remove tag from range. */
    if (inlen < 16)
        return SIZE_MAX;
    inlen -= 16;
    size_t textlen = inlen;

/* init the bits (we can always run in full), but use the last slot for calculating ek0, if possible */
#define AESECB6_INIT()                                                                                                             \
    do {                                                                                                                           \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits0 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits1 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits2 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits3 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        ctr = _mm_add_epi64(ctr, one8);                                                                                            \
        bits4 = _mm_shuffle_epi8(ctr, byteswap128);                                                                                \
        if (Ztls_LIKELY(inlen > 16 * 5)) {                                                                                         \
            ctr = _mm_add_epi64(ctr, one8);                                                                                        \
            bits5 = _mm_shuffle_epi8(ctr, byteswap128);                                                                            \
        } else {                                                                                                                   \
            bits5 = ek0;                                                                                                           \
            state |= STATE_EK0_READY;                                                                                              \
        }                                                                                                                          \
        __m128i k = ctx->super.ecb.keys.m128[0];                                                                                   \
        bits0 = _mm_xor_si128(bits0, k);                                                                                           \
        bits1 = _mm_xor_si128(bits1, k);                                                                                           \
        bits2 = _mm_xor_si128(bits2, k);                                                                                           \
        bits3 = _mm_xor_si128(bits3, k);                                                                                           \
        bits4 = _mm_xor_si128(bits4, k);                                                                                           \
        bits5 = _mm_xor_si128(bits5, k);                                                                                           \
    } while (0)

/* aes block update */
#define AESECB6_UPDATE(i)                                                                                                          \
    do {                                                                                                                           \
        __m128i k = ctx->super.ecb.keys.m128[i];                                                                                   \
        bits0 = _mm_aesenc_si128(bits0, k);                                                                                        \
        bits1 = _mm_aesenc_si128(bits1, k);                                                                                        \
        bits2 = _mm_aesenc_si128(bits2, k);                                                                                        \
        bits3 = _mm_aesenc_si128(bits3, k);                                                                                        \
        bits4 = _mm_aesenc_si128(bits4, k);                                                                                        \
        bits5 = _mm_aesenc_si128(bits5, k);                                                                                        \
    } while (0)

/* aesenclast */
#define AESECB6_FINAL(i)                                                                                                           \
    do {                                                                                                                           \
        __m128i k = ctx->super.ecb.keys.m128[i];                                                                                   \
        bits0 = _mm_aesenclast_si128(bits0, k);                                                                                    \
        bits1 = _mm_aesenclast_si128(bits1, k);                                                                                    \
        bits2 = _mm_aesenclast_si128(bits2, k);                                                                                    \
        bits3 = _mm_aesenclast_si128(bits3, k);                                                                                    \
        bits4 = _mm_aesenclast_si128(bits4, k);                                                                                    \
        bits5 = _mm_aesenclast_si128(bits5, k);                                                                                    \
    } while (0)

    struct aesgcm_context *agctx = reinterpret_cast<struct aesgcm_context *>(_ctx);
    uint8_t *output = static_cast<uint8_t *>(_output);
    const uint8_t *input = static_cast<const uint8_t *>(_input);

#define STATE_EK0_READY 0x1
    int32_t state = 0;

    /* setup ctr, retain Ek(0), len(A) | len(C) to be fed into GCM */
    __m128i ctr = calc_counter(agctx, seq);
    ctr = _mm_insert_epi32(ctr, 1, 0);
    __m128i ek0 = _mm_shuffle_epi8(ctr, byteswap128);
    __m128i ac = _mm_shuffle_epi8(_mm_set_epi32(0, (int)aadlen * 8, 0, (int)inlen * 8), byteswap128);

    struct fusion_aesgcm_context128 *ctx = reinterpret_cast<struct fusion_aesgcm_context128 *>(agctx->aesgcm);
    __m128i bits0, bits1, bits2, bits3, bits4, bits5 = _mm_setzero_si128();
    struct fusion_gfmul_state128 gstate = {0};

    /* Prepare first 6 blocks of bit stream, at the same time calculating ghash of AAD. */
    AESECB6_INIT();
    AESECB6_UPDATE(1);
    AESECB6_UPDATE(2);
    reduce_aad128(&gstate, ctx->ghash.data(), aad, aadlen);
    for (size_t i = 3; i < ctx->super.ecb.rounds; ++i)
        AESECB6_UPDATE(i);
    AESECB6_FINAL(ctx->super.ecb.rounds);

    /* Main loop. Operate in full blocks (6 * 16 bytes). */
    while (Ztls_LIKELY(inlen >= 6 * 16)) {
#define DECRYPT(i) _mm_storeu_si128(reinterpret_cast<__m128i *>(output + i * 16), _mm_xor_si128(bits##i, _mm_loadu_si128(reinterpret_cast<const __m128i *>(input + i * 16))))
        DECRYPT(0);
        DECRYPT(1);
        DECRYPT(2);
        DECRYPT(3);
        DECRYPT(4);
        DECRYPT(5);
#undef DECRYPT
#define GFMUL_NEXT(i) gfmul_nextstep128(&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>(input + i * 16)), ctx->ghash.data() + 5 - i)
        AESECB6_INIT();
        AESECB6_UPDATE(1);
        AESECB6_UPDATE(2);
        AESECB6_UPDATE(3);
        gfmul_firststep128(&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)input)), ctx->ghash.data() + 5);
        AESECB6_UPDATE(4);
        GFMUL_NEXT(1);
        AESECB6_UPDATE(5);
        GFMUL_NEXT(2);
        AESECB6_UPDATE(6);
        GFMUL_NEXT(3);
        AESECB6_UPDATE(7);
        GFMUL_NEXT(4);
        AESECB6_UPDATE(8);
        GFMUL_NEXT(5);
        AESECB6_UPDATE(9);
        gfmul_reduce128(&gstate);
        if (Ztls_UNLIKELY(ctx->super.ecb.rounds != 10)) {
            size_t i = 10;
            do {
                AESECB6_UPDATE(i);
            } while (++i < ctx->super.ecb.rounds);
        }
        AESECB6_FINAL(ctx->super.ecb.rounds);
        output += 6 * 16;
        input += 6 * 16;
        inlen -= 6 * 16;
#undef GFMUL_NEXT
    }

    /* Decrypt the remainder as well as finishing GHASH calculation. */
    if (inlen != 0) {
        struct fusion_aesgcm_ghash_precompute128 *ghash_precompute = ctx->ghash.data() + (inlen + 15) / 16 + 1;
#define ONEBLOCK(i)                                                                                                                \
    do {                                                                                                                           \
        if (inlen != 0) {                                                                                                          \
            __m128i b = inlen >= 16 ? _mm_loadu_si128(reinterpret_cast<const __m128i *>(input)) : loadn128(input, inlen);                                     \
            if (i == 0) {                                                                                                          \
                gfmul_firststep128(&gstate, b, --ghash_precompute);                                                                \
            } else {                                                                                                               \
                gfmul_nextstep128(&gstate, b, --ghash_precompute);                                                                 \
            }                                                                                                                      \
            b = _mm_xor_si128(b, bits##i);                                                                                         \
            if (inlen >= 16) {                                                                                                     \
                _mm_storeu_si128(reinterpret_cast<__m128i *>(output), b);                                                                               \
                output += 16;                                                                                                      \
                input += 16;                                                                                                       \
                inlen -= 16;                                                                                                       \
            } else {                                                                                                               \
                storen128(output, inlen, b);                                                                                       \
                output += inlen;                                                                                                   \
                input += inlen;                                                                                                    \
                inlen = 0;                                                                                                         \
            }                                                                                                                      \
        }                                                                                                                          \
    } while (0)
        ONEBLOCK(0);
        ONEBLOCK(1);
        ONEBLOCK(2);
        ONEBLOCK(3);
        ONEBLOCK(4);
        ONEBLOCK(5);
#undef ONEBLOCK
        gfmul_nextstep128(&gstate, ac, --ghash_precompute);
        assert(ghash_precompute == ctx->ghash.data());
    } else {
        gfmul_firststep128(&gstate, ac, ctx->ghash.data());
    }
    gfmul_reduce128(&gstate);

    /* Calculate EK0 if not yet available in bits5. */
    if ((state & STATE_EK0_READY) == 0) {
        bits5 = _mm_xor_si128(ek0, ctx->super.ecb.keys.m128[0]);
        for (size_t i = 1; i < ctx->super.ecb.rounds; ++i)
            bits5 = _mm_aesenc_si128(bits5, ctx->super.ecb.keys.m128[i]);
        bits5 = _mm_aesenclast_si128(bits5, ctx->super.ecb.keys.m128[ctx->super.ecb.rounds]);
    }

    /* Calculate GCM tag and compare. */
    __m128i calctag = gfmul_get_tag128(&gstate, bits5);
    __m128i recvtag = _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)input));
    if (_mm_movemask_epi8(_mm_cmpeq_epi8(calctag, recvtag)) != 0xffff)
        return SIZE_MAX;

    return textlen;

#undef AESECB6_INIT
#undef AESECB6_UPDATE
#undef AESECB6_FINAL
#undef STATE_EK0_READY
}

NO_SANITIZE_ADDRESS
static void non_temporal_encrypt_v256(struct st_aead_context_t *_ctx, void *_output, const Bytes *input, size_t incnt,
                                      uint64_t seq, const void *_aad, size_t aadlen)
{
/* init the bits (we can always run in full), but use the last slot for calculating ek0, if possible */
#define AESECB6_INIT()                                                                                                             \
    do {                                                                                                                           \
        ctr = _mm256_add_epi64(ctr, incr128x2);                                                                                    \
        bits0 = _mm256_shuffle_epi8(ctr, byteswap256);                                                                             \
        ctr = _mm256_add_epi64(ctr, incr128x2);                                                                                    \
        bits1 = _mm256_shuffle_epi8(ctr, byteswap256);                                                                             \
        ctr = _mm256_add_epi64(ctr, incr128x2);                                                                                    \
        bits2 = _mm256_shuffle_epi8(ctr, byteswap256);                                                                             \
        ctr = _mm256_add_epi64(ctr, incr128x2);                                                                                    \
        bits3 = _mm256_shuffle_epi8(ctr, byteswap256);                                                                             \
        ctr = _mm256_add_epi64(ctr, incr128x2);                                                                                    \
        bits4 = _mm256_shuffle_epi8(ctr, byteswap256);                                                                             \
        ctr = _mm256_add_epi64(ctr, incr128x2);                                                                                    \
        bits5 = _mm256_shuffle_epi8(ctr, byteswap256);                                                                             \
        if (Ztls_UNLIKELY(srclen <= 32 * 6 - 16) && src_vecleft == 0) {                                                            \
            bits5 = _mm256_permute2f128_si256(bits5, ac_ek0, 0x30);                                                                \
            state |= STATE_EK0_READY;                                                                                              \
        }                                                                                                                          \
        __m256i k = ctx->super.ecb.keys.m256[0];                                                                                   \
        bits0 = _mm256_xor_si256(bits0, k);                                                                                        \
        bits1 = _mm256_xor_si256(bits1, k);                                                                                        \
        bits2 = _mm256_xor_si256(bits2, k);                                                                                        \
        bits3 = _mm256_xor_si256(bits3, k);                                                                                        \
        bits4 = _mm256_xor_si256(bits4, k);                                                                                        \
        bits5 = _mm256_xor_si256(bits5, k);                                                                                        \
    } while (0)

/* aes block update */
#define AESECB6_UPDATE(i)                                                                                                          \
    do {                                                                                                                           \
        __m256i k = ctx->super.ecb.keys.m256[i];                                                                                   \
        bits0 = _mm256_aesenc_epi128(bits0, k);                                                                                    \
        bits1 = _mm256_aesenc_epi128(bits1, k);                                                                                    \
        bits2 = _mm256_aesenc_epi128(bits2, k);                                                                                    \
        bits3 = _mm256_aesenc_epi128(bits3, k);                                                                                    \
        bits4 = _mm256_aesenc_epi128(bits4, k);                                                                                    \
        bits5 = _mm256_aesenc_epi128(bits5, k);                                                                                    \
    } while (0)

/* aesenclast */
#define AESECB6_FINAL(i)                                                                                                           \
    do {                                                                                                                           \
        __m256i k = ctx->super.ecb.keys.m256[i];                                                                                   \
        bits0 = _mm256_aesenclast_epi128(bits0, k);                                                                                \
        bits1 = _mm256_aesenclast_epi128(bits1, k);                                                                                \
        bits2 = _mm256_aesenclast_epi128(bits2, k);                                                                                \
        bits3 = _mm256_aesenclast_epi128(bits3, k);                                                                                \
        bits4 = _mm256_aesenclast_epi128(bits4, k);                                                                                \
        bits5 = _mm256_aesenclast_epi128(bits5, k);                                                                                \
    } while (0)

    struct aesgcm_context *agctx = reinterpret_cast<struct aesgcm_context *>(_ctx);
    uint8_t *output = static_cast<uint8_t *>(_output);
    const uint8_t *aad = static_cast<const uint8_t *>(_aad);

#define STATE_EK0_READY 0x1
    int32_t state = 0;

    /* Bytes are written here first then written using NT store instructions, 64 bytes at a time. */
    uint8_t encbuf[32 * 9] __attribute__((aligned(32))), *encp;

    /* `encbuf` should be large enough to store up to 63-bytes of unaligned bytes, 6 16-byte AES blocks, plus AEAD tag that is
     * append to the ciphertext before writing the bytes to main memory using NT store instructions. */
    Ztls_BUILD_ASSERT(sizeof(encbuf) >= 64 + 6 * 32 + 16);

    /* load unaligned data within same cache line preceding `output`, adjusting pointers accordingly */
    encp = load_preceding_unaligned(encbuf, &output);

    /* setup ctr, retaining Ek(0), len(A) | len(C) to be fed into GCM */
    size_t total_length = aead__sum_iovec(input, incnt);
    __m256i ctr = _mm256_broadcastsi128_si256(calc_counter(agctx, seq));
    ctr = _mm256_insert_epi32(ctr, 1, 4);
    __m256i ac_ek0 = _mm256_permute2f128_si256(
        /* first half: ac */
        _mm256_castsi128_si256(_mm_shuffle_epi8(_mm_set_epi32(0, (int)aadlen * 8, 0, (int)total_length * 8), byteswap128)),
        /* second half: ek0 */
        _mm256_shuffle_epi8(ctr, byteswap256), 0x30);

    struct fusion_aesgcm_context256 *ctx = reinterpret_cast<struct fusion_aesgcm_context256 *>(agctx->aesgcm);
    __m256i bits0, bits1, bits2, bits3, bits4, bits5 = _mm256_setzero_si256();
    struct fusion_gfmul_state256 gstate = {0};

    /* find the first non-empty vec */
    const uint8_t *src = NULL;
    size_t srclen = 0, src_vecleft = incnt;
    while (srclen == 0 && src_vecleft != 0) {
        src = reinterpret_cast<const uint8_t *>(input[0].data());
        srclen = input[0].length();
        ++input;
        --src_vecleft;
    }

    /* Prepare first 6 blocks of bit stream, at the same time calculating ghash of AAD. */
    AESECB6_INIT();
    AESECB6_UPDATE(1);
    AESECB6_UPDATE(2);
    if (Ztls_LIKELY(aadlen != 0)) {
        union fusion_aesgcm_ghash_precompute256 *ghash_precompute;
        while (Ztls_UNLIKELY(aadlen >= 6 * 32)) {
            ghash_precompute = ctx->ghash.data() + 6;
            gfmul_firststep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)aad)), 0, --ghash_precompute);
            aad += 32;
            aadlen -= 32;
            for (int i = 1; i < 6; ++i) {
                gfmul_nextstep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)aad)), --ghash_precompute);
                aad += 32;
                aadlen -= 32;
            }
            gfmul_reduce256(&gstate);
        }
        if (Ztls_LIKELY(aadlen != 0)) {
            ghash_precompute = ctx->ghash.data() + (aadlen + 31) / 32;
            if (Ztls_UNLIKELY(aadlen >= 32)) {
                if (aadlen % 32 == 0 || aadlen % 32 > 16) {
                    gfmul_firststep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)aad)), 0, --ghash_precompute);
                    aad += 32;
                    aadlen -= 32;
                } else {
                    gfmul_firststep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)aad)), 1, --ghash_precompute);
                    aad += 16;
                    aadlen -= 16;
                }
                while (aadlen >= 32) {
                    gfmul_nextstep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)aad)), --ghash_precompute);
                    aad += 32;
                    aadlen -= 32;
                }
                if (Ztls_LIKELY(aadlen != 0)) {
                    assert(aadlen > 16);
                    gfmul_nextstep256(&gstate, loadn256(aad, aadlen), --ghash_precompute);
                }
            } else {
                gfmul_firststep256(&gstate, loadn256(aad, aadlen), aadlen <= 16, --ghash_precompute);
            }
            assert(ctx->ghash.data() == ghash_precompute);
            gfmul_reduce256(&gstate);
        }
    }
    for (size_t i = 3; i < ctx->super.ecb.rounds; ++i)
        AESECB6_UPDATE(i);
    AESECB6_FINAL(ctx->super.ecb.rounds);

    /* Main loop. This loop:
     *  1. using current keystream (bits0..bits5), xors a up to 6 * 16 bytes and writes to encbuf,
     *  2. then if there is no more data to be encrypted, exit the loop, otherwise,
     *  3. calculate ghash of the blocks being written to encbuf,
     *  4. calculate next 6 * 16 bytes of keystream,
     *  5. writes encbuf in 64-byte blocks
     * When exitting the loop, `remaining_ghash_from` represents the offset within `encbuf` from where ghash remains to be
     * calculated. */
    size_t remaining_ghash_from = encp - encbuf;
    if (srclen != 0) {
        while (1) {
            /* apply the bit stream to input, writing to encbuf */
            if (Ztls_LIKELY(srclen >= 6 * 32)) {
#define APPLY(i) _mm256_storeu_si256(reinterpret_cast<__m256i *>(encp + i * 32), _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + i * 32)), bits##i))
                APPLY(0);
                APPLY(1);
                APPLY(2);
                APPLY(3);
                APPLY(4);
                APPLY(5);
#undef APPLY
                encp += 6 * 32;
                src += 6 * 32;
                srclen -= 6 * 32;
                if (Ztls_UNLIKELY(srclen == 0)) {
                    if (src_vecleft == 0) {
                        remaining_ghash_from = (encp - encbuf) - 6 * 32;
                        break;
                    }
                    src = reinterpret_cast<const uint8_t *>(input[0].data());
                    srclen = input[0].length();
                    ++input;
                    --src_vecleft;
                }
            } else {
                /* slow path, load at most 6 * 32 bytes to encbuf then encrypt in-place */
                size_t bytes_copied = 0;
                do {
                    if (srclen >= 32 && bytes_copied < 5 * 32) {
                        _mm256_storeu_si256(reinterpret_cast<__m256i *>((void *)(encp + bytes_copied)), _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)src)));
                        bytes_copied += 32;
                        src += 32;
                        srclen -= 32;
                    } else {
                        encp[bytes_copied++] = *src++;
                        --srclen;
                    }
                    if (Ztls_UNLIKELY(srclen == 0)) {
                        do {
                            if (src_vecleft == 0)
                                break;
                            src = reinterpret_cast<const uint8_t *>(input[0].data());
                            srclen = input[0].length();
                            ++input;
                            --src_vecleft;
                        } while (srclen == 0);
                        if (srclen == 0)
                            break;
                    }
                } while (bytes_copied < 6 * 32);
#define APPLY(i)                                                                                                                   \
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(encp + i * 32), _mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(encp + i * 32)), bits##i))
                APPLY(0);
                APPLY(1);
                APPLY(2);
                APPLY(3);
                APPLY(4);
                APPLY(5);
#undef APPLY
                encp += bytes_copied;
                if (Ztls_UNLIKELY(srclen == 0)) {
                    /* Calculate amonut of data left to be ghashed, as well as zero-clearing the remainedr of partial block, as it
                     * will be fed into ghash. */
                    remaining_ghash_from = (encp - encbuf) - bytes_copied;
                    if ((bytes_copied & 15) != 0)
                        _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)encp), _mm_setzero_si128());
                    break;
                }
            }

            /* Next 96-byte block starts here. Run AES and ghash in parallel while writing output using non-temporal store
             * instructions. */
            AESECB6_INIT();
            union fusion_aesgcm_ghash_precompute256 *ghash_precompute = ctx->ghash.data() + 6;
            gfmul_firststep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encp - 6 * 32))), 0, --ghash_precompute);
            AESECB6_UPDATE(1);
            gfmul_nextstep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encp - 5 * 32))), --ghash_precompute);
            AESECB6_UPDATE(2);
            gfmul_nextstep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encp - 4 * 32))), --ghash_precompute);
            AESECB6_UPDATE(3);
            gfmul_nextstep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encp - 3 * 32))), --ghash_precompute);
            AESECB6_UPDATE(4);
            _mm256_stream_si256((void *)output, _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)encbuf)));
            _mm256_stream_si256((void *)(output + 32), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 32))));
            _mm256_stream_si256((void *)(output + 64), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 64))));
            _mm256_stream_si256((void *)(output + 96), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 96))));
            _mm256_stream_si256((void *)(output + 128), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 128))));
            _mm256_stream_si256((void *)(output + 160), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 160))));
            AESECB6_UPDATE(5);
            gfmul_nextstep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encp - 2 * 32))), --ghash_precompute);
            AESECB6_UPDATE(6);
            gfmul_nextstep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encp - 1 * 32))), --ghash_precompute);
            output += 192;
            encp -= 192;
            AESECB6_UPDATE(7);
            _mm256_store_si256(reinterpret_cast<__m256i *>((void *)encbuf), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 192))));
            AESECB6_UPDATE(8);
            _mm256_store_si256(reinterpret_cast<__m256i *>((void *)(encbuf + 32)), _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 224))));
            AESECB6_UPDATE(9);
            if (Ztls_UNLIKELY(ctx->super.ecb.rounds != 10)) {
                for (size_t i = 10; Ztls_LIKELY(i < ctx->super.ecb.rounds); ++i)
                    AESECB6_UPDATE(i);
            }
            assert(ctx->ghash.data() == ghash_precompute);
            gfmul_reduce256(&gstate);
            AESECB6_FINAL(ctx->super.ecb.rounds);
        }
    }

    /* Now, All the encrypted bits are built in encbuf. Calculate AEAD tag and append to encbuf. */

    { /* Run ghash against the remaining bytes, after appending `ac` (i.e., len(A) | len(C)). At this point, we might be ghashing 7
       * blocks at once. */
        size_t ac_off = remaining_ghash_from + ((encp - encbuf) - remaining_ghash_from + 15) / 16 * 16;
        _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)(encbuf + ac_off)), _mm256_castsi256_si128(ac_ek0));
        size_t blocks = ((encp - encbuf) - remaining_ghash_from + 15) / 16 + 1; /* round up, +1 for AC */
        assert(blocks <= 13);
        union fusion_aesgcm_ghash_precompute256 *ghash_precompute = ctx->ghash.data() + blocks / 2;
        if (blocks % 2 != 0) {
            gfmul_firststep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + remaining_ghash_from))), 1, ghash_precompute);
            remaining_ghash_from += 16;
        } else {
            gfmul_firststep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + remaining_ghash_from))), 0, --ghash_precompute);
            remaining_ghash_from += 32;
        }
        while (ghash_precompute != ctx->ghash.data()) {
            gfmul_nextstep256(&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + remaining_ghash_from))), --ghash_precompute);
            remaining_ghash_from += 32;
        }
        gfmul_reduce256(&gstate);
    }

    /* Calculate EK0, if in the unlikely case on not been done yet. When encoding in full size (16K), EK0 will be ready. */
    if (Ztls_UNLIKELY((state & STATE_EK0_READY) == 0)) {
        bits5 = ac_ek0;
        bits5 = _mm256_xor_si256(bits5, ctx->super.ecb.keys.m256[0]);
        for (size_t i = 1; i < ctx->super.ecb.rounds; ++i)
            bits5 = _mm256_aesenc_epi128(bits5, ctx->super.ecb.keys.m256[i]);
        bits5 = _mm256_aesenclast_epi128(bits5, ctx->super.ecb.keys.m256[ctx->super.ecb.rounds]);
    }

    /* append tag to encbuf */
    _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)encp),
                     gfmul_get_tag256(&gstate, _mm256_castsi256_si128(_mm256_permute2f128_si256(bits5, bits5, 0x11))));
    encp += 16;

    /* write remaining bytes */
    write_remaining_bytes(output, encbuf, encp);
}

static int non_temporal_setup(aead_context_t *_ctx, int is_enc, const void *key, const void *iv, size_t key_size)
{
    struct aesgcm_context *ctx = (struct aesgcm_context *)_ctx;
    int aesni256 = is_enc && fusion_can_aesni256;

    memset(&ctx->serial, 0, sizeof(ctx->serial));
    ctx->super.do_enc_begin = is_enc ? fusion_aead_enc_begin : NULL;
    ctx->super.do_enc_chunk = is_enc ? fusion_aead_enc_chunk : NULL;
    ctx->super.do_enc_end = is_enc ? fusion_aead_enc_end : NULL;
    ctx->super.do_dec_begin = is_enc ? NULL : fusion_aead_dec_begin;
    ctx->super.do_dec_chunk = is_enc ? NULL : fusion_aead_dec_chunk;
    ctx->super.do_dec_end = is_enc ? NULL : fusion_aead_dec_end;
    ctx->super.do_dec_abort = is_enc ? NULL : fusion_aead_dec_abort;
    ctx->static_iv = loadn128(iv, Ztls_AESGCM_IV_SIZE);
    ctx->static_iv = _mm_shuffle_epi8(ctx->static_iv, byteswap128);
    if (key == NULL)
        return 0;

    ctx->super.dispose_crypto = aesgcm_dispose_crypto;
    ctx->super.do_get_iv = aesgcm_get_iv;
    ctx->super.do_set_iv = aesgcm_set_iv;
    ctx->super.do_encrypt_init = NULL;
    ctx->super.do_encrypt_update = NULL;
    ctx->super.do_encrypt_final = NULL;
    if (is_enc) {
        ctx->super.do_encrypt = aead__do_encrypt;
        ctx->super.do_encrypt_v = aesni256 ? non_temporal_encrypt_v256 : non_temporal_encrypt_v128;
        ctx->super.do_encrypt_v_s = aead__do_encrypt_v_s;
        ctx->super.do_decrypt = NULL;
    } else {
        assert(!aesni256);
        ctx->super.do_encrypt = NULL;
        ctx->super.do_encrypt_v = NULL;
        ctx->super.do_encrypt_v_s = NULL;
        ctx->super.do_decrypt = non_temporal_decrypt128;
    }

    ctx->aesgcm =
        new_aesgcm(key, key_size,
                   7 * (fusion_can_aesni256 ? 32 : 16), // 6 blocks at once, plus len(A) | len(C) that we might append
                   aesni256);
    return 0;
}

static int non_temporal_aes128gcm_setup(aead_context_t *ctx, int is_enc, const void *key, const void *iv)
{
    return non_temporal_setup(ctx, is_enc, key, iv, Ztls_AES128_KEY_SIZE);
}

static int non_temporal_aes256gcm_setup(aead_context_t *ctx, int is_enc, const void *key, const void *iv)
{
    return non_temporal_setup(ctx, is_enc, key, iv, Ztls_AES256_KEY_SIZE);
}

aead_algorithm_t non_temporal_aes128gcm = {"AES128-GCM",
                                                     Ztls_AESGCM_CONFIDENTIALITY_LIMIT,
                                                     Ztls_AESGCM_INTEGRITY_LIMIT,
                                                     &fusion_aes128ctr,
                                                     NULL, // &fusion_aes128ecb,
                                                     Ztls_AES128_KEY_SIZE,
                                                     Ztls_AESGCM_IV_SIZE,
                                                     Ztls_AESGCM_TAG_SIZE,

                                                     1,
                                                     Ztls_X86_CACHE_LINE_ALIGN_BITS,
                                                     sizeof(struct aesgcm_context),
                                                     non_temporal_aes128gcm_setup,
                                                      1,
                                                      1};
aead_algorithm_t non_temporal_aes256gcm = {"AES256-GCM",
                                                     Ztls_AESGCM_CONFIDENTIALITY_LIMIT,
                                                     Ztls_AESGCM_INTEGRITY_LIMIT,
                                                     &fusion_aes256ctr,
                                                     NULL, // &fusion_aes128ecb,
                                                     Ztls_AES256_KEY_SIZE,
                                                     Ztls_AESGCM_IV_SIZE,
                                                     Ztls_AESGCM_TAG_SIZE,

                                                     1,
                                                     Ztls_X86_CACHE_LINE_ALIGN_BITS,
                                                     sizeof(struct aesgcm_context),
                                                     non_temporal_aes256gcm_setup,
                                                      1,
                                                      1};

#ifdef _WIN32
/**
 * fusion_is_supported_by_cpu:
 * Check that the CPU has extended instructions for PCMUL, AES and AVX2.
 * This test assumes that the CPU is following the x86/x64 architecture.
 * A slightly more refined test could check that the cpu_info spells out
 * "genuineIntel" or "authenticAMD", but would fail in presence of
 * little known CPU brands or some VM */
int fusion_is_supported_by_cpu(void)
{
    uint32_t cpu_info[4];
    uint32_t nb_ids;
    int is_supported = 0;

    __cpuid(cpu_info, 0);
    nb_ids = cpu_info[0];

    if (nb_ids >= 7) {
        uint32_t leaf1_ecx;
        __cpuid(cpu_info, 1);
        leaf1_ecx = cpu_info[2];

        if (/* PCLMUL */ (leaf1_ecx & (1 << 5)) != 0 && /* AES */ (leaf1_ecx & (1 << 25)) != 0) {
            uint32_t leaf7_ebx, leaf7_ecx;
            __cpuid(cpu_info, 7);
            leaf7_ebx = cpu_info[1];
            leaf7_ecx = cpu_info[2];

            is_supported = /* AVX2 */ (leaf7_ebx & (1 << 5)) != 0;

            /* enable 256-bit mode if possible */
            if (is_supported && (leaf7_ecx & 0x600) != 0 && !fusion_can_aesni256)
                fusion_can_aesni256 = 1;
        }
    }

    return is_supported;
}
#else
int fusion_is_supported_by_cpu(void)
{
    unsigned leaf1_ecx, leaf7_ebx, leaf7_ecx;

    { /* GCC-specific code to obtain CPU features */
        unsigned leaf_cnt;
        __asm__("cpuid" : "=a"(leaf_cnt) : "a"(0) : "ebx", "ecx", "edx");
        if (leaf_cnt < 7)
            return 0;
        __asm__("cpuid" : "=c"(leaf1_ecx) : "a"(1) : "ebx", "edx");
        __asm__("cpuid" : "=b"(leaf7_ebx), "=c"(leaf7_ecx) : "a"(7), "c"(0) : "edx");
    }

    /* AVX2 */
    if ((leaf7_ebx & (1 << 5)) == 0)
        return 0;
    /* AES */
    if ((leaf1_ecx & (1 << 25)) == 0)
        return 0;
    /* PCLMUL */
    if ((leaf1_ecx & (1 << 1)) == 0)
        return 0;

    /* enable 256-bit mode if possible */
    if ((leaf7_ecx & 0x600) != 0 && !fusion_can_aesni256)
        fusion_can_aesni256 = 1;

    return 1;
}
#endif

} // Ztls_

#endif
