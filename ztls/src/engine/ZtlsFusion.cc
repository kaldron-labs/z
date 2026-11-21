//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// This source file is licensed under the Apache License 2.0 *and* the MIT
// License. Please agree to *both* of the licensing terms!
//
//
// `transformH` function is a derivative work of OpenSSL. The original work
// is covered by the following license:
//
// Copyright 2013-2020 The OpenSSL Project Authors. All Rights Reserved.
//
// Licensed under the Apache License 2.0 (the "License").  You may not use
// this file except in compliance with the License.  You can obtain a copy
// in the file LICENSE in the source distribution or at
// https://www.openssl.org/source/license.html
//
//
// All other work, including modifications to the `transformH` function is
// covered by the following MIT license:
//
// Copyright (c) 2020-2022 Fastly, Kazuho Oku
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
#include <zlib/ZtlsEngine.hh>

#if ZtlsFusion

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#include <tmmintrin.h>
#include <nmmintrin.h>
#include <wmmintrin.h>

#include <zlib/ZuAssert.hh>

#include <zlib/ZmHeap.hh>

#include <zlib/ZtScratch.hh>

#include <zlib/ZtlsFusion.hh>
#include <zlib/ZtlsSec.hh>

namespace Ztls_ {

#if defined(__clang__)
#if __has_feature(address_sanitizer)
#define Ztls_NoSanitizeAddress __attribute__((no_sanitize("address")))
#endif
// gcc
#elif __SANITIZE_ADDRESS__
#define Ztls_NoSanitizeAddress __attribute__((no_sanitize_address))
#endif
#ifndef Ztls_NoSanitizeAddress
#define Ztls_NoSanitizeAddress
#endif

struct FusionGcmContext {
  FusionEcbContext	ecb{};
  size_t		capacity = 0;
  size_t		ghashCnt = 0;

  virtual ~FusionGcmContext() = default;
  virtual size_t grow(size_t count) = 0;
  virtual void seed(__m128i value) = 0;
  virtual void setupOne() = 0;
};

struct FusionGhash128 {
  __m128i	H;
  __m128i	r;
};

ZuDerive(Ghash128, (ZtArray<FusionGhash128, ZtArrayHeapID<"Ztls.Fusion.GHASH128">>));

struct FusionGcm128 : ZmNHeap<FusionGcm128>, FusionGcmContext {
friend inline ZuStringT<"Ztls.Fusion.GCM128"> ZmNHeapID(FusionGcm128 *);
  Ghash128	ghash;

  ~FusionGcm128() override { ZuClear(ghash.data(), ghash.length() * sizeof(*ghash.data())); }
  size_t grow(size_t count) override;
  void seed(__m128i value) override;
  void setupOne() override;
};

union FusionGhash256 {
  struct Narrow {
    __m128i	H[2];
    __m128i	r[2];
  } narrow;
  struct Wide {
    __m256i	Hx2;
    __m256i	rx2;
  } wide;
};

ZuDerive(Ghash256, (ZtArray<FusionGhash256, ZtArrayHeapID<"Ztls.Fusion.GHASH256">>));

struct FusionGcm256 : ZmNHeap<FusionGcm256>, FusionGcmContext {
friend inline ZuStringT<"Ztls.Fusion.GCM256"> ZmNHeapID(FusionGcm256 *);
  Ghash256	ghash;

  ~FusionGcm256() override { ZuClear(ghash.data(), ghash.length() * sizeof(*ghash.data())); }
  size_t grow(size_t count) override;
  void seed(__m128i value) override;
  void setupOne() override;
};

ZuAssert(alignof(FusionGcm128) >= 32);
ZuAssert(alignof(FusionGcm256) >= 32);

struct FusionCtrContext : ZmNHeap<FusionCtrContext>, CipherContext {
friend inline ZuStringT<"Ztls.Engine.Cipher.Fusion"> ZmNHeapID(FusionCtrContext *);
  FusionEcbContext	fusion;
  __m128i		bits;
  uint8_t		isReady = 0;

  FusionCtrContext(const CipherAlgo *algo, const void *key);
  ~FusionCtrContext() override;
  void init(const void *iv) override;
  void transform(void *output, const void *input, size_t len) override;
};

struct FusionAeadContext : ZmNHeap<FusionAeadContext>, AeadContext {
friend inline ZuStringT<"Ztls.Engine.AEAD.Fusion"> ZmNHeapID(FusionAeadContext *);
  FusionGcmContext	*aesgcm = nullptr;

  // retains the static IV in the upper 96 bits (in little endian)
  __m128i		staticIv;
  struct Serial {
    __m128i	j0, ctr, hash;
    uint8_t	bits[16], carry[16];
    size_t	bitUsed, carryUsed;
    uint64_t	aadlen, textlen;
    int		active;
  } serial{};
  FusionAeadContext(const AeadAlgo *algo, bool isEnc) : AeadContext{algo, isEnc} { }
  ~FusionAeadContext() override;
  bool serialEncrypt() const override { return isEnc; }
  bool serialDecrypt() const override { return !isEnc; }
  void getIV(void *iv) override;
  void setIV(const void *iv) override;
  int encBegin(uint64_t seq, const void *aad, size_t aadlen) override;
  int encChunk(void *output, const void *input, size_t length, size_t *written) override;
  int encEnd(void *output, size_t capacity, size_t *written) override;
  int decBegin(uint64_t seq, const void *aad, size_t aadlen) override;
  int decChunk(
    void *output, size_t capacity, const void *input, size_t length, size_t *written) override;
  int decEnd(const void *tag, size_t taglen) override;
  void decAbort() override;
  void encryptV(
    void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad,
    size_t aadlen) override;
  size_t decrypt(
    void *output, const void *input, size_t inlen, uint64_t seq, const void *aad,
    size_t aadlen) override;
  void encrypt(
    void *output, const void *input, size_t inlen, uint64_t seq, const void *aad, size_t aadlen,
    AeadSupplement *supp) override;
  void encryptVS(
    void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad, size_t aadlen,
    AeadSupplement *supp) override;
};

static const __m128i poly{1, int64_t(0xc200000000000000)};
static const __m128i byteswap128{0x08090a0b0c0d0e0f, 0x0001020304050607};
static const __m256i byteswap256{
  0x08090a0b0c0d0e0f, 0x0001020304050607, 0x08090a0b0c0d0e0f, 0x0001020304050607};
static const __m128i one8{1, 0};
static const __m256i incr128x2{2, 0, 2, 0};

// This function is covered by the Apache License and the MIT License. The origin is
// crypto/modes/asm/ghash-x86_64.pl of openssl at commit 33388b4.
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
  // pslldq          \$8,$T1
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

static inline __m128i gfmulDoReduce(__m128i hi, __m128i lo, __m128i mid)
{
  mid = _mm_xor_si128(mid, hi);
  mid = _mm_xor_si128(mid, lo);
  lo = _mm_xor_si128(lo, _mm_slli_si128(mid, 8));
  hi = _mm_xor_si128(hi, _mm_srli_si128(mid, 8));

  // fast reduction, using https://crypto.stanford.edu/RealWorldCrypto/slides/gueron.pdf
  __m128i r = _mm_clmulepi64_si128(lo, poly, 0x10);
  lo = _mm_shuffle_epi32(lo, 78);
  lo = _mm_xor_si128(lo, r);
  r = _mm_clmulepi64_si128(lo, poly, 0x10);
  lo = _mm_shuffle_epi32(lo, 78);
  lo = _mm_xor_si128(lo, r);
  lo = _mm_xor_si128(hi, lo);

  return lo;
}

struct FusionGfmul128 {
  __m128i	hi, lo, mid;
};

static inline __m128i xor128(__m128i x, __m128i y)
{
#if defined(__GNUC__) && !defined(__clang__)
  __m128i ret;
  __asm__("vpxor %2, %1, %0" : "=x"(ret) : "x"(x), "xm"(y));
  return ret;
#else
  return _mm_xor_si128(x, y);
#endif
}

static inline void
gfmulDoStep128(struct FusionGfmul128 *gstate, __m128i X, struct FusionGhash128 *precompute)
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

static inline void
gfmulFirststep128(struct FusionGfmul128 *gstate, __m128i X, struct FusionGhash128 *precompute)
{
  X = _mm_shuffle_epi8(X, byteswap128);
  X = _mm_xor_si128(gstate->lo, X);
  gstate->lo = _mm_setzero_si128();
  gstate->hi = _mm_setzero_si128();
  gstate->mid = _mm_setzero_si128();
  gfmulDoStep128(gstate, X, precompute);
}

static inline void
gfmulNextstep128(struct FusionGfmul128 *gstate, __m128i X, struct FusionGhash128 *precompute)
{
  X = _mm_shuffle_epi8(X, byteswap128);
  gfmulDoStep128(gstate, X, precompute);
}

static inline void gfmulReduce128(struct FusionGfmul128 *gstate)
{ gstate->lo = gfmulDoReduce(gstate->hi, gstate->lo, gstate->mid); }

static inline __m128i gfmulGetTag128(struct FusionGfmul128 *gstate, __m128i ek0)
{
  __m128i tag = _mm_shuffle_epi8(gstate->lo, byteswap128);
  tag = _mm_xor_si128(tag, ek0);
  return tag;
}

struct FusionGfmul256 {
  __m256i	hi, lo, mid;
};

static inline void
gfmulDoStep256(struct FusionGfmul256 *gstate, __m256i X, union FusionGhash256 *precompute)
{
  __m256i t = _mm256_clmulepi64_epi128(precompute->wide.Hx2, X, 0x00);
  gstate->lo = _mm256_xor_si256(gstate->lo, t);
  t = _mm256_clmulepi64_epi128(precompute->wide.Hx2, X, 0x11);
  gstate->hi = _mm256_xor_si256(gstate->hi, t);
  t = _mm256_shuffle_epi32(X, 78);
  t = _mm256_xor_si256(t, X);
  t = _mm256_clmulepi64_epi128(precompute->wide.rx2, t, 0x00);
  gstate->mid = _mm256_xor_si256(gstate->mid, t);
}

static inline void gfmulFirststep256(
  struct FusionGfmul256 *gstate, __m256i X, int half, union FusionGhash256 *precompute)
{
  X = _mm256_shuffle_epi8(X, byteswap256);
  X = _mm256_xor_si256(gstate->lo, X);
  if (half) X = _mm256_permute2f128_si256(X, X, 0x08);
  gstate->lo = _mm256_setzero_si256();
  gstate->hi = _mm256_setzero_si256();
  gstate->mid = _mm256_setzero_si256();
  gfmulDoStep256(gstate, X, precompute);
}

static inline void
gfmulNextstep256(struct FusionGfmul256 *gstate, __m256i X, union FusionGhash256 *precompute)
{
  X = _mm256_shuffle_epi8(X, byteswap256);
  gfmulDoStep256(gstate, X, precompute);
}

static inline void gfmulReduce256(struct FusionGfmul256 *gstate)
{
#define Ztls_Xor256to128(y) \
  _mm_xor_si128(_mm256_castsi256_si128(y), _mm256_extractf128_si256((y), 1))
  __m128i hi = Ztls_Xor256to128(gstate->hi);
  __m128i lo = Ztls_Xor256to128(gstate->lo);
  __m128i mid = Ztls_Xor256to128(gstate->mid);
#undef Ztls_Xor256to128

  lo = gfmulDoReduce(hi, lo, mid);
  gstate->lo = _mm256_zextsi128_si256(lo);
}

static inline __m128i gfmulGetTag256(struct FusionGfmul256 *gstate, __m128i ek0)
{
  __m128i tag = _mm_shuffle_epi8(_mm256_castsi256_si128(gstate->lo), byteswap128);
  tag = _mm_xor_si128(tag, ek0);
  return tag;
}

static inline __m128i aesecbEncrypt(FusionEcbContext *ctx, __m128i v)
{
#define Ztls_Roundkey(i) \
  (ctx->aesni256 ? _mm256_castsi256_si128(ctx->keys.m256[i]) : ctx->keys.m128[i])

  v = _mm_xor_si128(v, Ztls_Roundkey(0));
  for (size_t i = 1; i < ctx->rounds; ++i) v = _mm_aesenc_si128(v, Ztls_Roundkey(i));
  v = _mm_aesenclast_si128(v, Ztls_Roundkey(ctx->rounds));

  return v;

#undef Ztls_Roundkey
}

// 32-bytes of 0xff followed by 31-bytes of 0x00
static const uint8_t loadnMask[63] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t loadnShuffle[31] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
  0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, // first 16 bytes map to byte offsets
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80}; // latter 15 bytes map to zero

Ztls_NoSanitizeAddress static inline __m128i loadnEndOfPage(const void *p, size_t l)
{
  uintptr_t shift = (uintptr_t)p & 15;
  __m128i   pattern = _mm_loadu_si128((const __m128i *)(loadnShuffle + shift));
  return _mm_shuffle_epi8(_mm_load_si128((const __m128i *)((uintptr_t)p - shift)), pattern);
}

Ztls_NoSanitizeAddress static inline __m128i loadn128(const void *p, size_t l)
{
  __m128i   v, mask = _mm_loadu_si128((__m128i *)(loadnMask + 32 - l));
  uintptr_t mod4k = (uintptr_t)p % 4096;

  if (ZuLikely(mod4k <= 4096 - 16) || mod4k + l > 4096) {
    v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p));
  } else {
    v = loadnEndOfPage(p, l);
  }
  v = _mm_and_si128(v, mask);

  return v;
}

Ztls_NoSanitizeAddress static inline __m256i loadn256(const void *p, size_t l)
{
  __m256i   v, mask = _mm256_loadu_si256((__m256i *)(loadnMask + 32 - l));
  uintptr_t mod4k = (uintptr_t)p % 4096;

  if (ZuLikely(mod4k < 4096 - 32) || mod4k + l > 4096) {
    v = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
  } else if (l > 16) {
    __m128i first16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p)),
	    second16 = loadn128((uint8_t *)p + 16, l - 16);
    v = _mm256_permute2f128_si256(
      _mm256_castsi128_si256(first16), _mm256_castsi128_si256(second16), 0x20);
  } else if (l == 16) {
    v = _mm256_castsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i *>(p)));
  } else {
    v = _mm256_castsi128_si256(loadnEndOfPage(p, l));
  }
  v = _mm256_and_si256(v, mask);

  return v;
}

static inline void storen128(void *_p, size_t l, __m128i v)
{
  uint8_t buf[16], *p = static_cast<uint8_t *>(_p);

  *(__m128i *)buf = v;

  for (size_t i = 0; i != l; ++i) p[i] = buf[i];
}

struct FusionInput {
  const uint8_t	*ptr;
  size_t	len;
  const Bytes	*vec;
  size_t	vecIndex;
  size_t	vecCount;
};

static inline struct FusionInput inputVec(const Bytes *input, size_t incnt)
{ return (struct FusionInput){NULL, 0, input, 0, incnt}; }

static inline int inputNext(struct FusionInput *src)
{
  while (src->len == 0 && src->vecIndex != src->vecCount) {
    src->ptr = src->vec[src->vecIndex].data();
    src->len = src->vec[src->vecIndex].length();
    ++src->vecIndex;
  }
  return src->len != 0;
}

static size_t inputRead(struct FusionInput *src, uint8_t *dst, size_t max)
{
  size_t off = 0;

  while (off != max && inputNext(src)) {
    size_t len = max - off;
    if (len > src->len) len = src->len;
    memcpy(dst + off, src->ptr, len);
    src->ptr += len;
    src->len -= len;
    off += len;
  }
  return off;
}

static inline void inputConsume(struct FusionInput *src, size_t len)
{
  src->ptr += len;
  src->len -= len;
}

static inline void
ghashUpdate128(struct FusionGcm128 *ctx, struct FusionGfmul128 *gstate, __m128i block)
{
  gfmulFirststep128(gstate, block, ctx->ghash.data());
  gfmulReduce128(gstate);
}

static void aesgcmEncryptIter(
  FusionGcmContext *_ctx, void *output, struct FusionInput *input, size_t inlen, __m128i ctr,
  const void *_aad, size_t aadlen)
{
  struct FusionGcm128  *ctx = static_cast<struct FusionGcm128 *>(_ctx);
  const uint8_t	       *aad = static_cast<const uint8_t *>(_aad);
  uint8_t	       *dst = static_cast<uint8_t *>(output);
  struct FusionGfmul128 gstate = {0};
  size_t		aadlenTotal = aadlen;

  assert(!_ctx->ecb.aesni256);

  while (aadlen >= 16) {
    ghashUpdate128(ctx, &gstate, _mm_loadu_si128((const __m128i *)aad));
    aad += 16;
    aadlen -= 16;
  }
  if (aadlen != 0) ghashUpdate128(ctx, &gstate, loadn128(aad, aadlen));

  ctr = _mm_insert_epi32(ctr, 1, 0);
  __m128i ek0 = aesecbEncrypt(&ctx->ecb, _mm_shuffle_epi8(ctr, byteswap128));

  for (size_t left = inlen; left != 0;) {
    ctr = _mm_add_epi64(ctr, one8);
    __m128i bits = aesecbEncrypt(&ctx->ecb, _mm_shuffle_epi8(ctr, byteswap128)), enc;
    size_t  blocklen;

    if (inputNext(input) && input->len >= 16) {
      enc = _mm_xor_si128(_mm_loadu_si128((const __m128i *)input->ptr), bits);
      _mm_storeu_si128((__m128i *)dst, enc);
      inputConsume(input, 16);
      blocklen = 16;
    } else {
      uint8_t block[16] = {0}, encbuf[16] = {0};

      blocklen = inputRead(input, block, left < 16 ? left : 16);
      assert(blocklen != 0);
      enc = _mm_xor_si128(_mm_loadu_si128((const __m128i *)block), bits);
      storen128(dst, blocklen, enc);
      storen128(encbuf, blocklen, enc);
      enc = _mm_loadu_si128((const __m128i *)encbuf);
    }

    ghashUpdate128(ctx, &gstate, enc);
    dst += blocklen;
    left -= blocklen;
  }

  __m128i ac =
    _mm_shuffle_epi8(_mm_set_epi32(0, (int)aadlenTotal * 8, 0, (int)inlen * 8), byteswap128);
  ghashUpdate128(ctx, &gstate, ac);
  _mm_storeu_si128((__m128i *)dst, gfmulGetTag128(&gstate, ek0));
}

void fusionAesgcmEncrypt(
  FusionGcmContext *_ctx, void *output, const void *input, size_t inlen, __m128i ctr,
  const void *_aad, size_t aadlen, AeadSupplement *supp)
{
  auto fused = supp ? dynamic_cast<FusionCtrContext *>(supp->ctx) : nullptr;
  if (supp && (!fused || fused->fusion.rounds != _ctx->ecb.rounds)) {
    // Generic ciphers and different AES key widths use their own virtual operation.
    // The sample may include the tag, so compute it after all AEAD output exists.
    fusionAesgcmEncrypt(_ctx, output, input, inlen, ctr, _aad, aadlen, nullptr);
    aeadSupplement(supp);
    return;
  }
// init the bits (we can always run in full), but use the last slot for calculating ek0, if
// possible
#define Ztls_AESECB6Init() \
  do { \
    ctr = _mm_add_epi64(ctr, one8); \
    bits0 = _mm_shuffle_epi8(ctr, byteswap128); \
    ctr = _mm_add_epi64(ctr, one8); \
    bits1 = _mm_shuffle_epi8(ctr, byteswap128); \
    ctr = _mm_add_epi64(ctr, one8); \
    bits2 = _mm_shuffle_epi8(ctr, byteswap128); \
    ctr = _mm_add_epi64(ctr, one8); \
    bits3 = _mm_shuffle_epi8(ctr, byteswap128); \
    ctr = _mm_add_epi64(ctr, one8); \
    bits4 = _mm_shuffle_epi8(ctr, byteswap128); \
    if (ZuLikely(srclen > 16 * 5)) { \
      ctr = _mm_add_epi64(ctr, one8); \
      bits5 = _mm_shuffle_epi8(ctr, byteswap128); \
    } else { \
      if ((state & Ztls_StateEk0BeenFed) == 0) { \
	bits5 = ek0; \
	state |= Ztls_StateEk0BeenFed; \
      } \
      if ( \
	(state & Ztls_StateSuppUsed) != 0 && srclen <= 16 * 4 && \
	(const __m128i *)supp->input + 1 <= dstGhash \
      ) { \
	bits4 = _mm_loadu_si128(static_cast<const __m128i *>(supp->input)); \
	bits4keys = fused->fusion.keys.m128; \
	state |= Ztls_StateSuppInProcess; \
      } \
    } \
    __m128i k = ctx->ecb.keys.m128[0]; \
    bits0 = _mm_xor_si128(bits0, k); \
    bits1 = _mm_xor_si128(bits1, k); \
    bits2 = _mm_xor_si128(bits2, k); \
    bits3 = _mm_xor_si128(bits3, k); \
    bits4 = _mm_xor_si128(bits4, bits4keys[0]); \
    bits5 = _mm_xor_si128(bits5, k); \
  } while (0)

// aes block update
#define Ztls_AESECB6Update(i) \
  do { \
    __m128i k = ctx->ecb.keys.m128[i]; \
    bits0 = _mm_aesenc_si128(bits0, k); \
    bits1 = _mm_aesenc_si128(bits1, k); \
    bits2 = _mm_aesenc_si128(bits2, k); \
    bits3 = _mm_aesenc_si128(bits3, k); \
    bits4 = _mm_aesenc_si128(bits4, bits4keys[i]); \
    bits5 = _mm_aesenc_si128(bits5, k); \
  } while (0)

// aesenclast
#define Ztls_AESECB6Final(i) \
  do { \
    __m128i k = ctx->ecb.keys.m128[i]; \
    bits0 = _mm_aesenclast_si128(bits0, k); \
    bits1 = _mm_aesenclast_si128(bits1, k); \
    bits2 = _mm_aesenclast_si128(bits2, k); \
    bits3 = _mm_aesenclast_si128(bits3, k); \
    bits4 = _mm_aesenclast_si128(bits4, bits4keys[i]); \
    bits5 = _mm_aesenclast_si128(bits5, k); \
  } while (0)

  struct FusionGcm128 *ctx = static_cast<struct FusionGcm128 *>(_ctx);
  __m128i	       ek0, bits0, bits1, bits2, bits3, bits4, bits5 = _mm_setzero_si128();
  const __m128i	      *bits4keys = ctx->ecb.keys.m128;
  // is changed to supp->ctx.keys when calcurating suppout
  struct FusionGfmul128 gstate = {0};
  __m128i		gdatabuf[6];
  __m128i		ac =
    _mm_shuffle_epi8(_mm_set_epi32(0, (int)aadlen * 8, 0, (int)inlen * 8), byteswap128);

  // src and dst are updated after the chunk is processed
  const __m128i *src = static_cast<const __m128i *>(input);
  __m128i	*dst = static_cast<__m128i *>(output);
  size_t	 srclen = inlen;
  // aad and srcGhash are updated before the chunk is processed (i.e., when the pointers are fed
  // indo the processor)
  const __m128i *aad = static_cast<const __m128i *>(_aad), *dstGhash = dst;
  size_t	 dstGhashlen = srclen;

  struct FusionGhash128 *ghashPrecompute =
    ctx->ghash.data() + (aadlen + 15)/16 + (srclen + 15)/16 + 1;

#define Ztls_StateEk0BeenFed 0x3
#define Ztls_StateEk0Incomplete 0x2
#define Ztls_StateEk0Ready() ((state & Ztls_StateEk0BeenFed) == 0x1)
#define Ztls_StateSuppUsed 0x4
#define Ztls_StateSuppInProcess 0x8
  int32_t state = supp != NULL ? Ztls_StateSuppUsed : 0;

  // build counter
  ctr = _mm_insert_epi32(ctr, 1, 0);
  ek0 = _mm_shuffle_epi8(ctr, byteswap128);

  // start preparing AES
  Ztls_AESECB6Init();
  Ztls_AESECB6Update(1);

  // build first ghash data (only AAD can be fed at this point, as this would be calculated
  // alongside the first AES block)
  const __m128i *gdata = gdatabuf; // points to the elements fed into GHASH
  size_t	 gdataCnt = 0;
  if (ZuLikely(aadlen != 0)) {
    while (gdataCnt < 6) {
      if (ZuLikely(aadlen < 16)) {
	if (aadlen != 0) {
	  gdatabuf[gdataCnt++] = loadn128(aad, aadlen);
	  aadlen = 0;
	}
	goto MainLoop;
      }
      gdatabuf[gdataCnt++] = _mm_loadu_si128(aad++);
      aadlen -= 16;
    }
  }

  // the main loop
MainLoop:
  while (1) {
    // run AES and multiplication in parallel
    size_t i;
    for (i = 2; i < gdataCnt + 2; ++i) {
      Ztls_AESECB6Update(i);
      gfmulNextstep128(&gstate, _mm_loadu_si128(gdata++), --ghashPrecompute);
    }
    for (; i < ctx->ecb.rounds; ++i) Ztls_AESECB6Update(i);
    Ztls_AESECB6Final(i);

    // apply the bit stream to src and write to dest
    if (ZuLikely(srclen >= 6 * 16)) {
#define Ztls_Apply(i) \
  _mm_storeu_si128(dst + i, _mm_xor_si128(_mm_loadu_si128(src + i), bits##i))
      Ztls_Apply(0);
      Ztls_Apply(1);
      Ztls_Apply(2);
      Ztls_Apply(3);
      Ztls_Apply(4);
      Ztls_Apply(5);
#undef Ztls_Apply
      dst += 6;
      src += 6;
      srclen -= 6 * 16;
    } else {
      if ((state & Ztls_StateEk0BeenFed) == Ztls_StateEk0BeenFed) {
	ek0 = bits5;
	state &= ~Ztls_StateEk0Incomplete;
      }
      if ((state & Ztls_StateSuppInProcess) != 0) {
	_mm_storeu_si128((__m128i *)supp->output, bits4);
	state &= ~(Ztls_StateSuppUsed | Ztls_StateSuppInProcess);
      }
      if (srclen != 0) {
#define Ztls_Apply(i) \
  \
  do { \
    if (ZuLikely(srclen >= 16)) { \
      _mm_storeu_si128(dst++, _mm_xor_si128(_mm_loadu_si128(src++), bits##i)); \
      srclen -= 16; \
    } else if (ZuLikely(srclen != 0)) { \
      bits0 = bits##i; \
      goto ApplyRemainder; \
    } else { \
      goto ApplyEnd; \
    } \
  } while (0)
	Ztls_Apply(0);
	Ztls_Apply(1);
	Ztls_Apply(2);
	Ztls_Apply(3);
	Ztls_Apply(4);
	Ztls_Apply(5);
#undef Ztls_Apply
	goto ApplyEnd;
ApplyRemainder:
	storen128(dst, srclen, _mm_xor_si128(loadn128(src, srclen), bits0));
	dst = (__m128i *)((uint8_t *)dst + srclen);
	srclen = 0;
ApplyEnd:;
      }
    }

    // next block AES starts here
    Ztls_AESECB6Init();

    Ztls_AESECB6Update(1);

    // setup gdata
    if (ZuUnlikely(aadlen != 0)) {
      gdataCnt = 0;
      while (gdataCnt < 6) {
	if (aadlen < 16) {
	  if (aadlen != 0) {
	    gdatabuf[gdataCnt++] = loadn128(aad, aadlen);
	    aadlen = 0;
	  }
	  goto GdataFillDST;
	}
	gdatabuf[gdataCnt++] = _mm_loadu_si128(aad++);
	aadlen -= 16;
      }
      gdata = gdatabuf;
    } else if (ZuLikely(dstGhashlen >= 6 * 16)) {
      gdata = dstGhash;
      gdataCnt = 6;
      dstGhash += 6;
      dstGhashlen -= 96;
    } else {
      gdataCnt = 0;
GdataFillDST:
      while (gdataCnt < 6) {
	if (dstGhashlen < 16) {
	  if (dstGhashlen != 0) {
	    gdatabuf[gdataCnt++] = loadn128(dstGhash, dstGhashlen);
	    dstGhashlen = 0;
	  }
	  if (gdataCnt < 6) goto Finish;
	  break;
	}
	gdatabuf[gdataCnt++] = _mm_loadu_si128(dstGhash++);
	dstGhashlen -= 16;
      }
      gdata = gdatabuf;
    }
  }

Finish:
  gdatabuf[gdataCnt++] = ac;

  // We have complete set of data to be fed into GHASH. Let's finish the remaining calculation.
  // Note that by now, all AES operations for payload encryption and ek0 are complete. This is is
  // because it is necessary for GCM to process at least the same amount of data (i.e.
  // payload-blocks + AC), and because AES is at least one 96-byte block ahead.
  assert(Ztls_StateEk0Ready());
  for (size_t i = 0; i < gdataCnt; ++i) gfmulNextstep128(&gstate, gdatabuf[i], --ghashPrecompute);

  gfmulReduce128(&gstate);
  _mm_storeu_si128(dst, gfmulGetTag128(&gstate, ek0));

  // Finish the calculation of supplemental vector. Done at the very last, because the sample
  // might cover the GCM tag.
  if ((state & Ztls_StateSuppUsed) != 0) {
    size_t i;
    if ((state & Ztls_StateSuppInProcess) == 0) {
      bits4keys = fused->fusion.keys.m128;
      bits4 = _mm_xor_si128(
	_mm_loadu_si128(reinterpret_cast<const __m128i *>(supp->input)), bits4keys[0]);
      i = 1;
    } else {
      i = 2;
    }
    do {
      bits4 = _mm_aesenc_si128(bits4, bits4keys[i++]);
    } while (i != ctx->ecb.rounds);
    bits4 = _mm_aesenclast_si128(bits4, bits4keys[i]);
    _mm_storeu_si128((__m128i *)supp->output, bits4);
  }

#undef Ztls_AESECB6Init
#undef Ztls_AESECB6Update
#undef Ztls_AESECB6Final
#undef Ztls_StateEk0Ready
#undef Ztls_StateSuppInProcess
}

int fusionAesgcmDecrypt(
  FusionGcmContext *_ctx, void *output, const void *input, size_t inlen, __m128i ctr,
  const void *_aad, size_t aadlen, const void *tag)
{
  struct FusionGcm128 *ctx = static_cast<struct FusionGcm128 *>(_ctx);
  __m128i ek0 = _mm_setzero_si128(), bits0, bits1 = _mm_setzero_si128(),
	  bits2 = _mm_setzero_si128(), bits3 = _mm_setzero_si128(), bits4 = _mm_setzero_si128(),
	  bits5 = _mm_setzero_si128();
  struct FusionGfmul128 gstate = {0};
  __m128i		gdatabuf[6];
  __m128i		ac =
    _mm_shuffle_epi8(_mm_set_epi32(0, (int)aadlen * 8, 0, (int)inlen * 8), byteswap128);
  struct FusionGhash128 *ghashPrecompute =
    ctx->ghash.data() + (aadlen + 15)/16 + (inlen + 15)/16 + 1;

  const __m128i *gdata; // points to the elements fed into GHASH
  size_t	 gdataCnt;

  const __m128i *srcGhash = static_cast<const __m128i *>(input),
		*srcAes = static_cast<const __m128i *>(input),
		*aad = static_cast<const __m128i *>(_aad);
  __m128i *dst = static_cast<__m128i *>(output);
  size_t   nondataAesCnt = 0, srcGhashlen = inlen, srcAeslen = inlen;

  // schedule ek0 and suppkey
  ctr = _mm_add_epi64(ctr, one8);
  bits0 = _mm_xor_si128(_mm_shuffle_epi8(ctr, byteswap128), ctx->ecb.keys.m128[0]);
  ++nondataAesCnt;

#define Ztls_StateIsFirstRun 0x1
#define Ztls_StateGhashHasMore 0x2
  int state = Ztls_StateIsFirstRun | Ztls_StateGhashHasMore;

  // the main loop
  while (1) {
    // setup gdata
    if (ZuUnlikely(aadlen != 0)) {
      gdata = gdatabuf;
      gdataCnt = 0;
      while (gdataCnt < 6) {
	if (aadlen < 16) {
	  if (aadlen != 0) {
	    gdatabuf[gdataCnt++] = loadn128(aad, aadlen);
	    aadlen = 0;
	    ++nondataAesCnt;
	  }
	  goto GdataFillSrc;
	}
	gdatabuf[gdataCnt++] = _mm_loadu_si128(aad++);
	aadlen -= 16;
	++nondataAesCnt;
      }
    } else if (ZuLikely(srcGhashlen >= 6 * 16)) {
      gdata = srcGhash;
      gdataCnt = 6;
      srcGhash += 6;
      srcGhashlen -= 6 * 16;
    } else {
      gdata = gdatabuf;
      gdataCnt = 0;
GdataFillSrc:
      while (gdataCnt < 6) {
	if (srcGhashlen < 16) {
	  if (srcGhashlen != 0) {
	    gdatabuf[gdataCnt++] = loadn128(srcGhash, srcGhashlen);
	    srcGhash = (__m128i *)((uint8_t *)srcGhash + srcGhashlen);
	    srcGhashlen = 0;
	  }
	  if (gdataCnt < 6 && (state & Ztls_StateGhashHasMore) != 0) {
	    gdatabuf[gdataCnt++] = ac;
	    state &= ~Ztls_StateGhashHasMore;
	  }
	  break;
	}
	gdatabuf[gdataCnt++] = _mm_loadu_si128(srcGhash++);
	srcGhashlen -= 16;
      }
    }

    // setup aes bits
    if (ZuLikely(nondataAesCnt == 0)) goto InitAllBits;
    switch (nondataAesCnt) {
#define Ztls_InitBits(n, keys) \
  case n: \
    ctr = _mm_add_epi64(ctr, one8); \
    bits##n = _mm_xor_si128(_mm_shuffle_epi8(ctr, byteswap128), keys.m128[0]);
InitAllBits:
      Ztls_InitBits(0, ctx->ecb.keys);
      Ztls_InitBits(1, ctx->ecb.keys);
      Ztls_InitBits(2, ctx->ecb.keys);
      Ztls_InitBits(3, ctx->ecb.keys);
      Ztls_InitBits(4, ctx->ecb.keys);
      Ztls_InitBits(5, ctx->ecb.keys);
#undef Ztls_InitBits
    }

    {
      // run aes and ghash
#define Ztls_AESECB6Update(i) \
  do { \
    __m128i k = ctx->ecb.keys.m128[i]; \
    bits0 = _mm_aesenc_si128(bits0, k); \
    bits1 = _mm_aesenc_si128(bits1, k); \
    bits2 = _mm_aesenc_si128(bits2, k); \
    bits3 = _mm_aesenc_si128(bits3, k); \
    bits4 = _mm_aesenc_si128(bits4, k); \
    bits5 = _mm_aesenc_si128(bits5, k); \
  } while (0)

      size_t aesi;
      for (aesi = 1; aesi <= gdataCnt; ++aesi) {
	Ztls_AESECB6Update(aesi);
	gfmulNextstep128(&gstate, _mm_loadu_si128(gdata++), --ghashPrecompute);
      }
      for (; aesi < ctx->ecb.rounds; ++aesi) Ztls_AESECB6Update(aesi);
      __m128i k = ctx->ecb.keys.m128[aesi];
      bits0 = _mm_aesenclast_si128(bits0, k);
      bits1 = _mm_aesenclast_si128(bits1, k);
      bits2 = _mm_aesenclast_si128(bits2, k);
      bits3 = _mm_aesenclast_si128(bits3, k);
      bits4 = _mm_aesenclast_si128(bits4, k);
      bits5 = _mm_aesenclast_si128(bits5, k);

#undef Ztls_AESECB6Update
    }

    // apply aes bits
    if (ZuLikely(nondataAesCnt == 0 && srcAeslen >= 6 * 16)) {
#define Ztls_Apply(i) \
  _mm_storeu_si128(dst + i, _mm_xor_si128(_mm_loadu_si128(srcAes + i), bits##i))
      Ztls_Apply(0);
      Ztls_Apply(1);
      Ztls_Apply(2);
      Ztls_Apply(3);
      Ztls_Apply(4);
      Ztls_Apply(5);
#undef Ztls_Apply
      dst += 6;
      srcAes += 6;
      srcAeslen -= 6 * 16;
    } else {
      if ((state & Ztls_StateIsFirstRun) != 0) {
	ek0 = bits0;
	state &= ~Ztls_StateIsFirstRun;
      }
      switch (nondataAesCnt) {
#define Ztls_Apply(i) \
  \
  case i: \
    if (ZuLikely(srcAeslen > 16)) { \
      _mm_storeu_si128(dst++, _mm_xor_si128(_mm_loadu_si128(srcAes++), bits##i)); \
      srcAeslen -= 16; \
    } else { \
      bits0 = bits##i; \
      goto Finish; \
    }
	Ztls_Apply(0);
	Ztls_Apply(1);
	Ztls_Apply(2);
	Ztls_Apply(3);
	Ztls_Apply(4);
	Ztls_Apply(5);
#undef Ztls_Apply
      }
      nondataAesCnt = 0;
    }
  }

Finish:
  if (srcAeslen == 16) {
    _mm_storeu_si128(dst, _mm_xor_si128(_mm_loadu_si128(srcAes), bits0));
  } else if (srcAeslen != 0) {
    storen128(dst, srcAeslen, _mm_xor_si128(loadn128(srcAes, srcAeslen), bits0));
  }

  assert((state & Ztls_StateIsFirstRun) == 0);

  // the only case where AES operation is complete and GHASH is not is when the application of AC
  // is remaining
  if ((state & Ztls_StateGhashHasMore) != 0) {
    assert(ghashPrecompute - 1 == ctx->ghash.data());
    gfmulNextstep128(&gstate, ac, --ghashPrecompute);
  }

  gfmulReduce128(&gstate);
  __m128i calctag = gfmulGetTag128(&gstate, ek0);

  return _mm_movemask_epi8(_mm_cmpeq_epi8(
	   calctag, _mm_loadu_si128(reinterpret_cast<const __m128i *>(tag)))) == 0xffff;

#undef Ztls_StateIsFirstRun
#undef Ztls_StateGhashHasMore
}

static __m128i expandKey(__m128i key, __m128i temp)
{
  key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
  key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
  key = _mm_xor_si128(key, _mm_slli_si128(key, 4));

  key = _mm_xor_si128(key, temp);

  return key;
}

ZuDerive(Aes128Keys, (ZtArray<__m128i, ZtArrayHeapID<"Ztls.Fusion.AES128Keys">>));
ZuDerive(Aes256Keys, (ZtArray<__m256i, ZtArrayHeapID<"Ztls.Fusion.AES256Keys">>));

void fusionAesecbInit(
  FusionEcbContext *ctx, int isEnc, const void *key, size_t keySize, int aesni256)
{
  if (ctx->keys.m128) fusionAesecbDispose(ctx);
  assert(isEnc && "decryption is not supported (yet)");

  size_t i = 0;

  switch (keySize) {
  case 16:
    // AES128
    ctx->rounds = 10;
    break;
  case 32:
    // AES256
    ctx->rounds = 14;
    break;
  default:
    assert(!"invalid key size; AES128 / AES256 are supported");
    break;
  }
  ctx->aesni256 = aesni256;

  Aes128Keys keys128;
  keys128.length(ctx->rounds + 1);
  // load and expand 128-bit keys before any AVX2 broadcast
  keys128[i++] = _mm_loadu_si128((__m128i *)key);
  if (keySize == 32) keys128[i++] = _mm_loadu_si128((__m128i *)key + 1);
  while (1) {
#define Ztls_Expand(R) \
  { \
    keys128[i] = expandKey( \
      keys128[i - keySize/16], \
      _mm_shuffle_epi32(_mm_aeskeygenassist_si128(keys128[i - 1], R), _MM_SHUFFLE(3, 3, 3, 3))); \
    if (i == ctx->rounds) break; \
    ++i; \
    if (keySize > 24) { \
      keys128[i] = expandKey( \
	keys128[i - keySize/16], \
	_mm_shuffle_epi32( \
	  _mm_aeskeygenassist_si128(keys128[i - 1], R), _MM_SHUFFLE(2, 2, 2, 2))); \
      ++i; \
    } \
  }
    Ztls_Expand(0x1);
    Ztls_Expand(0x2);
    Ztls_Expand(0x4);
    Ztls_Expand(0x8);
    Ztls_Expand(0x10);
    Ztls_Expand(0x20);
    Ztls_Expand(0x40);
    Ztls_Expand(0x80);
    Ztls_Expand(0x1b);
    Ztls_Expand(0x36);
#undef Ztls_Expand
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

FusionEcbContext::~FusionEcbContext()
{
  if (keys.m128) fusionAesecbDispose(this);
}

void fusionAesecbDispose(FusionEcbContext *ctx)
{
  if (!ctx->keys.m128) return;
  if (ctx->aesni256) {
    ZuClear(ctx->keys.m256, (ctx->rounds + 1) * sizeof(__m256i));
    Aes256Keys::free(ctx->keys.m256);
  } else {
    ZuClear(ctx->keys.m128, (ctx->rounds + 1) * sizeof(__m128i));
    Aes128Keys::free(ctx->keys.m128);
  }
  ctx->keys.m128 = nullptr;
  ctx->rounds = 0;
  ctx->aesni256 = 0;
}

void fusionAesecbEncrypt(FusionEcbContext *ctx, void *dst, const void *src)
{
  __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src));
  v = aesecbEncrypt(ctx, v);
  _mm_storeu_si128(reinterpret_cast<__m128i *>(dst), v);
}

// returns the number of ghash entries that is required to handle an AEAD block of given size
static size_t aesgcmCalcGhashCnt(size_t capacity)
{
  // round-up by block size, add to handle worst split of the size between AAD and payload, plus
  // context to hash AC
  return (capacity + 15)/16 + 2;
}

static void setupGhashEntry(__m128i *h, __m128i *r, const __m128i *prev, __m128i h0)
{
  if (prev) *h = gfmul(*prev, h0);
  *r = _mm_xor_si128(_mm_shuffle_epi32(*h, 78), *h);
}

void FusionGcm128::seed(__m128i value) { ghash[0].H = value; }
void FusionGcm128::setupOne()
{
  setupGhashEntry(
    &ghash[ghashCnt].H, &ghash[ghashCnt].r, ghashCnt ? &ghash[ghashCnt - 1].H : nullptr,
    ghash[0].H);
  ++ghashCnt;
}

void FusionGcm256::seed(__m128i value) { ghash[0].narrow.H[1] = value; }
void FusionGcm256::setupOne()
{
  auto slot = [this](size_t i) { return &ghash[i/2].narrow.H[i % 2 == 0]; };
  setupGhashEntry(
    slot(ghashCnt), &ghash[ghashCnt/2].narrow.r[ghashCnt % 2 == 0],
    ghashCnt ? slot(ghashCnt - 1) : nullptr, ghash[0].narrow.H[1]);
  ++ghashCnt;
}

template <typename Array> static void growGhash(Array &ghash, size_t count)
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

size_t FusionGcm128::grow(size_t count)
{
  growGhash(ghash, count);
  return count;
}

size_t FusionGcm256::grow(size_t count)
{
  count += count % 2;
  growGhash(ghash, count/2);
  return count;
}

FusionGcmContext *fusionAesgcmNew(const void *key, size_t keySize, size_t capacity, int aesni256)
{
  FusionGcmContext *ctx;
  if (aesni256) {
    auto storage = new FusionGcm256{};
    ZmAssert(!(uintptr_t(storage) % alignof(FusionGcm256)));
    ctx = storage;
  } else {
    auto storage = new FusionGcm128{};
    ZmAssert(!(uintptr_t(storage) % alignof(FusionGcm128)));
    ctx = storage;
  }
  fusionAesecbInit(&ctx->ecb, 1, key, keySize, aesni256);

  size_t  count = aesgcmCalcGhashCnt(capacity);
  __m128i H0 = aesecbEncrypt(&ctx->ecb, _mm_setzero_si128());
  H0 = transformH(_mm_shuffle_epi8(H0, byteswap128));
  count = ctx->grow(count);
  ctx->seed(H0);
  ctx->capacity = capacity;
  while (ctx->ghashCnt < count) ctx->setupOne();
  return ctx;
}

FusionGcmContext *fusionAesgcmSetCapacity(FusionGcmContext *ctx, size_t capacity)
{
  size_t count = aesgcmCalcGhashCnt(capacity);
  if (count <= ctx->ghashCnt) return ctx;
  count = ctx->grow(count);
  ctx->capacity = capacity;
  while (ctx->ghashCnt < count) ctx->setupOne();
  return ctx;
}

void fusionAesgcmFree(FusionGcmContext *ctx) { delete ctx; }

FusionCtrContext::FusionCtrContext(const CipherAlgo *algo, const void *key) : CipherContext{algo}
{
  // The header-protection block does not benefit from the 256-bit AES path.
  fusionAesecbInit(&fusion, 1, key, algo->keySize, 0);
}

FusionCtrContext::~FusionCtrContext() { ZuClear(&bits, sizeof(bits)); }

void FusionCtrContext::init(const void *iv)
{
  _mm_storeu_si128(
    &bits, aesecbEncrypt(&fusion, _mm_loadu_si128(reinterpret_cast<const __m128i *>(iv))));
  isReady = 1;
}

void FusionCtrContext::transform(void *output, const void *input, size_t len)
{
  assert(isReady && len <= 16);
  isReady = 0;
  if (len < 16)
    storen128(output, len, _mm_xor_si128(_mm_loadu_si128(&bits), loadn128(input, len)));
  else
    _mm_storeu_si128(
      reinterpret_cast<__m128i *>(output),
      _mm_xor_si128(
	_mm_loadu_si128(&bits), _mm_loadu_si128(reinterpret_cast<const __m128i *>(input))));
}

struct FusionCipherAlgo : CipherAlgo {
  using CipherAlgo::CipherAlgo;
  CipherContext *create(int, const void *key) const override
  { return new FusionCtrContext{this, key}; }
};

static void aesgcmEnsureCapacity(struct FusionAeadContext *ctx, size_t textlen, size_t aadlen)
{
  if (aadlen > SIZE_MAX - textlen) abort();

  size_t required = textlen + aadlen;
  if (required > ctx->aesgcm->capacity) { fusionAesgcmSetCapacity(ctx->aesgcm, required); }
}

static inline __m128i calcCounter(struct FusionAeadContext *ctx, uint64_t seq)
{
  __m128i ctr = _mm_setzero_si128();
  ctr = _mm_insert_epi64(ctr, seq, 0);
  ctr = _mm_slli_si128(ctr, 4);
  ctr = _mm_xor_si128(ctx->staticIv, ctr);
  return ctr;
}

// The serial path never allocates or reads outside the supplied destination span.
static void serialHash(struct FusionAeadContext *ctx, __m128i block)
{
  __m128i h = ctx->aesgcm->ecb.aesni256 ?
    ((struct FusionGcm256 *)ctx->aesgcm)->ghash[0].narrow.H[1] :
    ((struct FusionGcm128 *)ctx->aesgcm)->ghash[0].H;
  ctx->serial.hash =
    gfmul(_mm_xor_si128(ctx->serial.hash, _mm_shuffle_epi8(block, byteswap128)), h);
}

static int serialError(struct FusionAeadContext *ctx)
{
  ZuClear(&ctx->serial, sizeof(ctx->serial));
  return Ztls_ErrorLibrary;
}

int fusionAeadEncBegin(AeadContext *_ctx, uint64_t seq, const void *_aad, size_t aadlen)
{
  struct FusionAeadContext *ctx = static_cast<FusionAeadContext *>(_ctx);
  const uint8_t		   *aad = static_cast<const uint8_t *>(_aad);
  ZuClear(&ctx->serial, sizeof(ctx->serial));
  if (aadlen > UINT64_MAX/8 || (aadlen != 0 && aad == NULL)) return serialError(ctx);
  ctx->serial.aadlen = aadlen;
  ctx->serial.ctr = _mm_insert_epi32(calcCounter(ctx, seq), 1, 0);
  ctx->serial.j0 = _mm_shuffle_epi8(ctx->serial.ctr, byteswap128);
  ctx->serial.bitUsed = 16;
  ctx->serial.active = 1;
  while (aadlen >= 16) {
    serialHash(ctx, _mm_loadu_si128(reinterpret_cast<const __m128i *>((const void *)aad)));
    aad += 16;
    aadlen -= 16;
  }
  if (aadlen != 0) {
    uint8_t block[16] = {0};
    memcpy(block, aad, aadlen);
    serialHash(ctx, _mm_loadu_si128(reinterpret_cast<const __m128i *>((const void *)block)));
  }
  return 0;
}

// Full-block kernel keeps six AES pipelines in flight, using the selected expanded keys.
static void serialBlocks(
  struct FusionAeadContext *ctx, uint8_t *dst, const uint8_t *src, int wide, int decrypt)
{
  FusionEcbContext *ecb = &ctx->aesgcm->ecb;
  if (wide) {
    struct FusionGcm256	 *gctx = static_cast<struct FusionGcm256 *>(ctx->aesgcm);
    struct FusionGfmul256 gstate = {.lo = _mm256_zextsi128_si256(ctx->serial.hash)};
    __m256i		  bits[6];
    for (size_t i = 0; i != 6; ++i) {
      ctx->serial.ctr = _mm_add_epi32(ctx->serial.ctr, _mm_set_epi32(0, 0, 0, 1));
      __m128i first = _mm_shuffle_epi8(ctx->serial.ctr, byteswap128);
      ctx->serial.ctr = _mm_add_epi32(ctx->serial.ctr, _mm_set_epi32(0, 0, 0, 1));
      bits[i] = _mm256_inserti128_si256(
	_mm256_castsi128_si256(first), _mm_shuffle_epi8(ctx->serial.ctr, byteswap128), 1);
      bits[i] = _mm256_xor_si256(bits[i], ecb->keys.m256[0]);
    }
    for (size_t r = 1; r < ecb->rounds; ++r)
      for (size_t i = 0; i != 6; ++i) bits[i] = _mm256_aesenc_epi128(bits[i], ecb->keys.m256[r]);
    for (size_t i = 0; i != 6; ++i) {
      __m256i enc = _mm256_xor_si256(
	_mm256_aesenclast_epi128(bits[i], ecb->keys.m256[ecb->rounds]),
	_mm256_loadu_si256(reinterpret_cast<const __m256i *>((const void *)(src + 32 * i))));
      __m256i auth = decrypt ?
	_mm256_loadu_si256(reinterpret_cast<const __m256i *>((const void *)(src + 32 * i))) :
	enc;
      if (ctx->algo->nonTemporal && ((uintptr_t)(dst + 32 * i) & 31) == 0)
	_mm256_stream_si256((void *)(dst + 32 * i), enc);
      else
	_mm256_storeu_si256(reinterpret_cast<__m256i *>((void *)(dst + 32 * i)), enc);
      if (i == 0)
	gfmulFirststep256(&gstate, auth, 0, gctx->ghash.data() + 5);
      else
	gfmulNextstep256(&gstate, auth, gctx->ghash.data() + 5 - i);
    }
    gfmulReduce256(&gstate);
    ctx->serial.hash = _mm256_castsi256_si128(gstate.lo);
  } else {
    struct FusionGcm128	 *gctx = static_cast<struct FusionGcm128 *>(ctx->aesgcm);
    struct FusionGfmul128 gstate = {.lo = ctx->serial.hash};
    __m128i		  bits[6];
    for (size_t i = 0; i != 6; ++i) {
      ctx->serial.ctr = _mm_add_epi32(ctx->serial.ctr, _mm_set_epi32(0, 0, 0, 1));
      bits[i] = _mm_xor_si128(_mm_shuffle_epi8(ctx->serial.ctr, byteswap128), ecb->keys.m128[0]);
    }
    for (size_t r = 1; r < ecb->rounds; ++r)
      for (size_t i = 0; i != 6; ++i) bits[i] = _mm_aesenc_si128(bits[i], ecb->keys.m128[r]);
    for (size_t i = 0; i != 6; ++i) {
      __m128i enc = _mm_xor_si128(
	_mm_aesenclast_si128(bits[i], ecb->keys.m128[ecb->rounds]),
	_mm_loadu_si128(reinterpret_cast<const __m128i *>((const void *)(src + 16 * i))));
      __m128i auth = decrypt ?
	_mm_loadu_si128(reinterpret_cast<const __m128i *>((const void *)(src + 16 * i))) :
	enc;
      if (ctx->algo->nonTemporal && ((uintptr_t)(dst + 16 * i) & 15) == 0)
	_mm_stream_si128((void *)(dst + 16 * i), enc);
      else
	_mm_storeu_si128(reinterpret_cast<__m128i *>((void *)(dst + 16 * i)), enc);
      if (i == 0)
	gfmulFirststep128(&gstate, auth, gctx->ghash.data() + 5);
      else
	gfmulNextstep128(&gstate, auth, gctx->ghash.data() + 5 - i);
    }
    gfmulReduce128(&gstate);
    ctx->serial.hash = gstate.lo;
  }
}

static int serialChunk(
  AeadContext *_ctx, void *_output, const void *_input, size_t length, size_t *written,
  int decrypt)
{
  struct FusionAeadContext *ctx = static_cast<FusionAeadContext *>(_ctx);
  uint8_t		   *dst = static_cast<uint8_t *>(_output);
  const uint8_t		   *src = static_cast<const uint8_t *>(_input);
  *written = 0;
  if (!ctx->serial.active || length > UINT64_MAX/8 - ctx->serial.textlen ||
    (length != 0 && (dst == NULL || src == NULL)))
    return serialError(ctx);
  ctx->serial.textlen += length;
  size_t left = length, batch = ctx->aesgcm->ecb.aesni256 ? 192 : 96;
  while (left != 0) {
    if (ctx->serial.bitUsed == 16 && left >= batch) {
      serialBlocks(ctx, dst, src, ctx->aesgcm->ecb.aesni256, decrypt);
      dst += batch;
      src += batch;
      left -= batch;
      continue;
    }
    if (ctx->serial.bitUsed == 16) {
      ctx->serial.ctr = _mm_add_epi32(ctx->serial.ctr, _mm_set_epi32(0, 0, 0, 1));
      _mm_storeu_si128(
	reinterpret_cast<__m128i *>((void *)ctx->serial.bits),
	aesecbEncrypt(&ctx->aesgcm->ecb, _mm_shuffle_epi8(ctx->serial.ctr, byteswap128)));
      ctx->serial.bitUsed = 0;
    }
    size_t n = 16 - ctx->serial.bitUsed;
    if (n > left) n = left;
    for (size_t i = 0; i != n; ++i) {
      uint8_t c = src[i];
      uint8_t p = c ^ ctx->serial.bits[ctx->serial.bitUsed++];
      dst[i] = p;
      ctx->serial.carry[ctx->serial.carryUsed++] = decrypt ? c : p;
    }
    if (ctx->serial.carryUsed == 16) {
      serialHash(
	ctx, _mm_loadu_si128(reinterpret_cast<const __m128i *>((const void *)ctx->serial.carry)));
      ctx->serial.carryUsed = 0;
    }
    dst += n;
    src += n;
    left -= n;
  }
  if (ctx->algo->nonTemporal) _mm_sfence();
  *written = length;
  return 0;
}

int fusionAeadEncChunk(
  AeadContext *ctx, void *output, const void *input, size_t length, size_t *written)
{ return serialChunk(ctx, output, input, length, written, 0); }

int fusionAeadDecChunk(
  AeadContext *ctx, void *output, size_t capacity, const void *input, size_t length,
  size_t *written)
{
  *written = 0;
  if (length > capacity) return serialError(static_cast<FusionAeadContext *>(ctx));
  return serialChunk(ctx, output, input, length, written, 1);
}

int fusionAeadEncEnd(AeadContext *_ctx, void *output, size_t capacity, size_t *written)
{
  struct FusionAeadContext *ctx = static_cast<FusionAeadContext *>(_ctx);
  *written = 0;
  if (!ctx->serial.active || capacity < 16 || output == NULL) return serialError(ctx);
  if (ctx->serial.carryUsed != 0) {
    memset(ctx->serial.carry + ctx->serial.carryUsed, 0, 16 - ctx->serial.carryUsed);
    serialHash(
      ctx, _mm_loadu_si128(reinterpret_cast<const __m128i *>((const void *)ctx->serial.carry)));
  }
  __m128i lengths = _mm_set_epi64x(ctx->serial.aadlen * 8, ctx->serial.textlen * 8);
  serialHash(ctx, _mm_shuffle_epi8(lengths, byteswap128));
  _mm_storeu_si128(
    reinterpret_cast<__m128i *>(output),
    _mm_xor_si128(
      _mm_shuffle_epi8(ctx->serial.hash, byteswap128),
      aesecbEncrypt(&ctx->aesgcm->ecb, ctx->serial.j0)));
  ZuClear(&ctx->serial, sizeof(ctx->serial));
  *written = 16;
  return 0;
}

int fusionAeadDecBegin(AeadContext *ctx, uint64_t seq, const void *aad, size_t aadlen)
{ return fusionAeadEncBegin(ctx, seq, aad, aadlen); }

int fusionAeadDecEnd(AeadContext *ctx, const void *tag, size_t taglen)
{
  uint8_t expected[16];
  size_t  written;
  if (tag == NULL || taglen != sizeof(expected))
    return serialError(static_cast<FusionAeadContext *>(ctx));
  int ret = fusionAeadEncEnd(ctx, expected, sizeof(expected), &written);
  if (ret == 0 &&
    !Ztls::ctEqual(
      {expected, sizeof(expected)}, {static_cast<const uint8_t *>(tag), sizeof(expected)}))
    ret = Ztls_AlertBadRecordMac;
  ZuClear(expected, sizeof(expected));
  return ret;
}

void fusionAeadDecAbort(AeadContext *ctx) { serialError(static_cast<FusionAeadContext *>(ctx)); }

void aeadDoEncrypt(
  AeadContext *_ctx, void *output, const void *input, size_t inlen, uint64_t seq, const void *aad,
  size_t aadlen, AeadSupplement *supp)
{
  struct FusionAeadContext *ctx = static_cast<FusionAeadContext *>(_ctx);

  aesgcmEnsureCapacity(ctx, inlen, aadlen);
  fusionAesgcmEncrypt(
    ctx->aesgcm, output, input, inlen, calcCounter(ctx, seq), aad, aadlen, supp);
}

ZuDerive(FusionOverlap, (ZtBArray<ZtArrayHeapID<"Ztls.Fusion.OverlapScratch">>));

static void aeadDoEncryptV(
  AeadContext *_ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq,
  const void *aad, size_t aadlen)
{
  size_t inlen = aeadVecSize(input, incnt);
  int	 useScratch = 0;

  if (incnt == 1) {
    aeadDoEncrypt(_ctx, output, input[0].data(), input[0].length(), seq, aad, aadlen, NULL);
    return;
  }

  size_t off = 0;
  for (size_t i = 0; i != incnt; ++i) {
    uintptr_t outStart = (uintptr_t)output, outEnd = outStart + inlen;
    uintptr_t inStart = (uintptr_t)input[i].data(), inEnd = inStart + input[i].length();

    if (input[i].length() != 0 && inStart < outEnd && outStart < inEnd &&
      input[i].data() != (uint8_t *)output + off) {
      useScratch = 1;
      break;
    }
    off += input[i].length();
  }

  auto encrypt = [_ctx, output, input, incnt, inlen, seq, aad, aadlen](uint8_t *dst)
  {
    size_t offset = 0;
    for (size_t i = 0; i != incnt; ++i) {
      if (input[i].length() != 0) memmove(dst + offset, input[i].data(), input[i].length());
      offset += input[i].length();
    }
    aeadDoEncrypt(_ctx, output, dst, inlen, seq, aad, aadlen, NULL);
  };

  if (!useScratch) {
    encrypt(static_cast<uint8_t *>(output));
    return;
  }

  // Ordinary TLS records fit the scratch API. Keep the original wide length
  // for direct backend calls with exceptionally large vectors.
  unsigned size = inlen <= UINT_MAX ? unsigned(inlen) : 0;
  auto	   scratch = ZtScratch(FusionOverlap, size, size);
  if (!size) scratch.length(inlen);
  ZuGuard clear{[&scratch]() { ZuClear(scratch.data(), scratch.length()); }};
  encrypt(scratch.data());
}

static void aeadDoEncryptVS(
  AeadContext *_ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq,
  const void *aad, size_t aadlen, AeadSupplement *supp)
{
  struct FusionAeadContext *ctx = static_cast<FusionAeadContext *>(_ctx);
  size_t		    inlen = aeadVecSize(input, incnt);
  struct FusionInput	    inputIter = inputVec(input, incnt);

  aesgcmEnsureCapacity(ctx, inlen, aadlen);

  aesgcmEncryptIter(
    ctx->aesgcm, output, &inputIter, inlen, calcCounter(ctx, seq), aad, aadlen);
  aeadSupplement(supp);
}

static size_t aeadDoDecrypt(
  AeadContext *_ctx, void *output, const void *input, size_t inlen, uint64_t seq, const void *aad,
  size_t aadlen)
{
  struct FusionAeadContext *ctx = static_cast<FusionAeadContext *>(_ctx);

  if (inlen < 16) return SIZE_MAX;

  size_t enclen = inlen - 16;
  aesgcmEnsureCapacity(ctx, enclen, aadlen);
  if (!fusionAesgcmDecrypt(
	ctx->aesgcm, output, input, enclen, calcCounter(ctx, seq), aad, aadlen,
	(const uint8_t *)input + enclen))
    return SIZE_MAX;
  return enclen;
}

static inline void aesgcmGetIV(AeadContext *_ctx, void *iv)
{
  struct FusionAeadContext *ctx = static_cast<FusionAeadContext *>(_ctx);

  __m128i m128 = _mm_shuffle_epi8(ctx->staticIv, byteswap128);
  storen128(iv, Ztls_AESGCMIVSize, m128);
}

static inline void aesgcmSetIV(AeadContext *_ctx, const void *iv)
{
  struct FusionAeadContext *ctx = static_cast<FusionAeadContext *>(_ctx);

  ctx->staticIv = loadn128(iv, Ztls_AESGCMIVSize);
  ctx->staticIv = _mm_shuffle_epi8(ctx->staticIv, byteswap128);
}

static int
aesgcmSetup(AeadContext *_ctx, int isEnc, const void *key, const void *iv, size_t keySize)
{
  struct FusionAeadContext *ctx = static_cast<FusionAeadContext *>(_ctx);

  memset(&ctx->serial, 0, sizeof(ctx->serial));

  ctx->staticIv = loadn128(iv, Ztls_AESGCMIVSize);
  ctx->staticIv = _mm_shuffle_epi8(ctx->staticIv, byteswap128);
  if (key == NULL) return 0;

  ctx->aesgcm = fusionAesgcmNew(
    key, keySize,
    1500
    // assume ordinary packet size
    ,
    0
    // no support for aesni256 yet
  );
  return 0;
}

static int nonTemporalSetup(AeadContext *, int, const void *, const void *, size_t);
struct FusionAeadAlgo : AeadAlgo {
  using AeadAlgo::AeadAlgo;
  AeadContext *create(bool isEnc, const void *key, const void *iv) const override
  {
    auto ctx = new FusionAeadContext{this, isEnc};
    int	 ret = nonTemporal ? nonTemporalSetup(ctx, isEnc, key, iv, keySize) :
			     aesgcmSetup(ctx, isEnc, key, iv, keySize);
    if (ret) {
      delete ctx;
      return nullptr;
    }
    return ctx;
  }
};

int			      fusionCanAesni256 = 0;
static const FusionCipherAlgo aes128ctrAlgo{"AES128-CTR", Ztls_AES128KeySize, 1, Ztls_AESIVSize};
const CipherAlgo	     &fusionAes128ctr = aes128ctrAlgo;
static const FusionCipherAlgo aes256ctrAlgo{"AES256-CTR", Ztls_AES256KeySize, 1, Ztls_AESIVSize};
const CipherAlgo	     &fusionAes256ctr = aes256ctrAlgo;
static const FusionAeadAlgo   aes128gcmAlgo{"AES128-GCM", Ztls_AESGCMConfLimit,
  Ztls_AESGCMIntegrityLimit, &fusionAes128ctr,
  NULL, // &fusion_aes128ecb,
  Ztls_AES128KeySize, Ztls_AESGCMIVSize, Ztls_AESGCMTagSize, 0, 0,

  1, 1};
const AeadAlgo		     &fusionAes128gcm = aes128gcmAlgo;
static const FusionAeadAlgo   aes256gcmAlgo{"AES256-GCM", Ztls_AESGCMConfLimit,
  Ztls_AESGCMIntegrityLimit, &fusionAes256ctr,
  NULL, // &fusion_aes256ecb,
  Ztls_AES256KeySize, Ztls_AESGCMIVSize, Ztls_AESGCMTagSize, 0, 0,

  1, 1};
const AeadAlgo		     &fusionAes256gcm = aes256gcmAlgo;

static inline void reduceAad128(
  struct FusionGfmul128 *gstate, struct FusionGhash128 *ghash, const void *_aad, size_t aadlen)
{
  struct FusionGhash128 *ghashPrecompute;
  const uint8_t		*aad = static_cast<const uint8_t *>(_aad);

  while (ZuUnlikely(aadlen >= 6 * 16)) {
    ghashPrecompute = ghash + 6;
    gfmulFirststep128(
      gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)aad)), --ghashPrecompute);
    aad += 16;
    aadlen -= 16;
    for (int i = 1; i < 6; ++i) {
      gfmulNextstep128(
	gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)aad)),
	--ghashPrecompute);
      aad += 16;
      aadlen -= 16;
    }
    gfmulReduce128(gstate);
  }

  if (ZuLikely(aadlen != 0)) {
    ghashPrecompute = ghash + (aadlen + 15)/16;
    if (ZuUnlikely(aadlen >= 16)) {
      gfmulFirststep128(
	gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)aad)),
	--ghashPrecompute);
      aad += 16;
      aadlen -= 16;
      while (aadlen >= 16) {
	gfmulNextstep128(
	  gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)aad)),
	  --ghashPrecompute);
	aad += 16;
	aadlen -= 16;
      }
      if (ZuLikely(aadlen != 0))
	gfmulNextstep128(gstate, loadn128(aad, aadlen), --ghashPrecompute);
    } else {
      gfmulFirststep128(gstate, loadn128(aad, aadlen), --ghashPrecompute);
    }
    assert(ghash == ghashPrecompute);
    gfmulReduce128(gstate);
  }
}

Ztls_NoSanitizeAddress static inline uint8_t *
loadPrecedingUnaligned(uint8_t *encbuf, uint8_t **output)
{
  uint8_t *encp;

  if ((encp = encbuf + ((uintptr_t)*output & 63)) != encbuf) {
    _mm256_store_si256(
      reinterpret_cast<__m256i *>((void *)encbuf),
      _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(*output - (encp - encbuf)))));
    _mm256_store_si256(
      reinterpret_cast<__m256i *>((void *)(encbuf + 32)),
      _mm256_load_si256(
	reinterpret_cast<const __m256i *>((void *)(*output - (encp - encbuf) + 32))));
    *output -= encp - encbuf;
  }

  return encp;
}

Ztls_NoSanitizeAddress static inline void
writeRemainingBytes(uint8_t *dst, const uint8_t *src, const uint8_t *end)
{
  // Write in 64-byte chunks, using NT store instructions. Last partial block, if any, is written
  // to cache, as that cache line would likely be read when the next TLS record is being built.

  for (; end - src >= 64; dst += 64, src += 64) {
    _mm256_stream_si256(
      (void *)dst, _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)src)));
    _mm256_stream_si256(
      (void *)(dst + 32),
      _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(src + 32))));
  }
  _mm_sfence();
  // weakly ordered writes have to be synced before being passed to NIC
  if (src != end) {
    for (; end - src >= 16; dst += 16, src += 16)
      _mm_store_si128(
	reinterpret_cast<__m128i *>((void *)dst),
	_mm_load_si128(reinterpret_cast<const __m128i *>((void *)src)));
    if (src != end) storen128((void *)dst, end - src, loadn128((void *)src, end - src));
  }
}

Ztls_NoSanitizeAddress static void nonTemporalEncryptV128(
  AeadContext *_ctx, void *_output, const Bytes *input, size_t incnt, uint64_t seq,
  const void *aad, size_t aadlen)
{
// init the bits (we can always run in full), but use the last slot for calculating ek0, if
// possible
#define Ztls_AESECB6Init() \
  do { \
    ctr = _mm_add_epi64(ctr, one8); \
    bits0 = _mm_shuffle_epi8(ctr, byteswap128); \
    ctr = _mm_add_epi64(ctr, one8); \
    bits1 = _mm_shuffle_epi8(ctr, byteswap128); \
    ctr = _mm_add_epi64(ctr, one8); \
    bits2 = _mm_shuffle_epi8(ctr, byteswap128); \
    ctr = _mm_add_epi64(ctr, one8); \
    bits3 = _mm_shuffle_epi8(ctr, byteswap128); \
    ctr = _mm_add_epi64(ctr, one8); \
    bits4 = _mm_shuffle_epi8(ctr, byteswap128); \
    if (ZuLikely(srclen > 16 * 5) || srcVecleft != 0) { \
      ctr = _mm_add_epi64(ctr, one8); \
      bits5 = _mm_shuffle_epi8(ctr, byteswap128); \
    } else { \
      bits5 = ek0; \
      state |= Ztls_StateEk0Ready; \
    } \
    __m128i k = ctx->ecb.keys.m128[0]; \
    bits0 = _mm_xor_si128(bits0, k); \
    bits1 = _mm_xor_si128(bits1, k); \
    bits2 = _mm_xor_si128(bits2, k); \
    bits3 = _mm_xor_si128(bits3, k); \
    bits4 = _mm_xor_si128(bits4, k); \
    bits5 = _mm_xor_si128(bits5, k); \
  } while (0)

// aes block update
#define Ztls_AESECB6Update(i) \
  do { \
    __m128i k = ctx->ecb.keys.m128[i]; \
    bits0 = _mm_aesenc_si128(bits0, k); \
    bits1 = _mm_aesenc_si128(bits1, k); \
    bits2 = _mm_aesenc_si128(bits2, k); \
    bits3 = _mm_aesenc_si128(bits3, k); \
    bits4 = _mm_aesenc_si128(bits4, k); \
    bits5 = _mm_aesenc_si128(bits5, k); \
  } while (0)

// aesenclast
#define Ztls_AESECB6Final(i) \
  do { \
    __m128i k = ctx->ecb.keys.m128[i]; \
    bits0 = _mm_aesenclast_si128(bits0, k); \
    bits1 = _mm_aesenclast_si128(bits1, k); \
    bits2 = _mm_aesenclast_si128(bits2, k); \
    bits3 = _mm_aesenclast_si128(bits3, k); \
    bits4 = _mm_aesenclast_si128(bits4, k); \
    bits5 = _mm_aesenclast_si128(bits5, k); \
  } while (0)

  struct FusionAeadContext *agctx = static_cast<FusionAeadContext *>(_ctx);
  uint8_t		   *output = static_cast<uint8_t *>(_output);

#define Ztls_StateEk0Ready 0x1
#define Ztls_StateCopy128b 0x2
  int32_t state = 0;

  // Bytes are written here first then written using NT store instructions, 64 bytes at a time.
  uint8_t encbuf[32 * 6] __attribute__((aligned(32))), *encp;

  // `encbuf` should be large enough to store up to 63-bytes of unaligned bytes, 6 16-byte AES
  // blocks, plus AEAD tag that is append to the ciphertext before writing the bytes to main
  // memory using NT store instructions.
  ZuAssert(sizeof(encbuf) >= 64 + 6 * 16 + 16);

  // load unaligned data within same cache line preceding `output`, adjusting pointers accordingly
  encp = loadPrecedingUnaligned(encbuf, &output);

  // First write would be 128 bytes (32+6*16), if encbuf contains no less than 32 bytes already.
  if (encp - encbuf >= 32) state |= Ztls_StateCopy128b;

  // setup ctr, retain Ek(0), len(A) | len(C) to be fed into GCM
  size_t  totalLen = aeadVecSize(input, incnt);
  __m128i ctr = calcCounter(agctx, seq);
  ctr = _mm_insert_epi32(ctr, 1, 0);
  __m128i ek0 = _mm_shuffle_epi8(ctr, byteswap128);
  __m128i ac =
    _mm_shuffle_epi8(_mm_set_epi32(0, (int)aadlen * 8, 0, (int)totalLen * 8), byteswap128);

  struct FusionGcm128  *ctx = static_cast<struct FusionGcm128 *>(agctx->aesgcm);
  __m128i		bits0, bits1, bits2, bits3, bits4, bits5 = _mm_setzero_si128();
  struct FusionGfmul128 gstate = {0};

  // find the first non-empty vec
  const uint8_t *src = NULL;
  size_t	 srclen = 0, srcVecleft = incnt;
  while (srclen == 0 && srcVecleft != 0) {
    src = reinterpret_cast<const uint8_t *>(input[0].data());
    srclen = input[0].length();
    ++input;
    --srcVecleft;
  }

  // Prepare first 6 blocks of bit stream, at the same time calculating ghash of AAD.
  Ztls_AESECB6Init();
  Ztls_AESECB6Update(1);
  Ztls_AESECB6Update(2);
  reduceAad128(&gstate, ctx->ghash.data(), aad, aadlen);
  for (size_t i = 3; i < ctx->ecb.rounds; ++i) Ztls_AESECB6Update(i);
  Ztls_AESECB6Final(ctx->ecb.rounds);

  //  Main loop. This loop:
  //  1. using current keystream (bits0..bits5), xors a up to 6 * 16 bytes and writes to encbuf,
  //  2. then if there is no more data to be encrypted, exit the loop, otherwise,
  //  3. calculate ghash of the blocks being written to encbuf,
  //  4. calculate next 6 * 16 bytes of keystream,
  //  5. writes encbuf in 64-byte blocks
  // When exitting the loop, `remainingGhashFrom` represents the offset within `encbuf` from
  // where ghash remains to be
  // calculated.
  size_t remainingGhashFrom = encp - encbuf;
  if (srclen != 0) {
    while (1) {
      // apply the bit stream to input, writing to encbuf
      if (ZuLikely(srclen >= 6 * 16)) {
#define Ztls_Apply(i) \
  \
  _mm_storeu_si128( \
    reinterpret_cast<__m128i *>(encp + i * 16), \
    _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i * 16)), bits##i))
	Ztls_Apply(0);
	Ztls_Apply(1);
	Ztls_Apply(2);
	Ztls_Apply(3);
	Ztls_Apply(4);
	Ztls_Apply(5);
#undef Ztls_Apply
	encp += 6 * 16;
	src += 6 * 16;
	srclen -= 6 * 16;
	if (ZuUnlikely(srclen == 0)) {
	  if (srcVecleft == 0) {
	    remainingGhashFrom = (encp - encbuf) - 96;
	    break;
	  }
	  src = reinterpret_cast<const uint8_t *>(input[0].data());
	  srclen = input[0].length();
	  ++input;
	  --srcVecleft;
	}
      } else {
	// slow path, load at most 6 * 16 bytes to encbuf then encrypt in-place
	size_t bytesCopied = 0;
	do {
	  if (srclen >= 16 && bytesCopied < 5 * 16) {
	    _mm_storeu_si128(
	      reinterpret_cast<__m128i *>((void *)(encp + bytesCopied)),
	      _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)src)));
	    bytesCopied += 16;
	    src += 16;
	    srclen -= 16;
	  } else {
	    encp[bytesCopied++] = *src++;
	    --srclen;
	  }
	  if (ZuUnlikely(srclen == 0)) {
	    do {
	      if (srcVecleft == 0) break;
	      src = reinterpret_cast<const uint8_t *>(input[0].data());
	      srclen = input[0].length();
	      ++input;
	      --srcVecleft;
	    } while (srclen == 0);
	    if (srclen == 0) break;
	  }
	} while (bytesCopied < 6 * 16);
#define Ztls_Apply(i) \
  \
  _mm_storeu_si128( \
    reinterpret_cast<__m128i *>(encp + i * 16), \
    _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i *>(encp + i * 16)), bits##i))
	Ztls_Apply(0);
	Ztls_Apply(1);
	Ztls_Apply(2);
	Ztls_Apply(3);
	Ztls_Apply(4);
	Ztls_Apply(5);
#undef Ztls_Apply
	encp += bytesCopied;
	if (ZuUnlikely(srclen == 0)) {
	  // Calculate amonut of data left to be ghashed, as well as zero-clearing the remainedr
	  // of partial block, as it will be fed into ghash.
	  remainingGhashFrom = (encp - encbuf) - bytesCopied;
	  if ((bytesCopied & 15) != 0)
	    _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)encp), _mm_setzero_si128());
	  break;
	}
      }

      // Next 96-byte block starts here. Run AES and ghash in while writing output using
      // non-temporal stores in 64-byte blocks.
      Ztls_AESECB6Init();
      struct FusionGhash128 *ghashPrecompute = ctx->ghash.data() + 6;
      gfmulFirststep128(
	&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encp - 6 * 16))),
	--ghashPrecompute);
      Ztls_AESECB6Update(1);
      gfmulNextstep128(
	&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encp - 5 * 16))),
	--ghashPrecompute);
      Ztls_AESECB6Update(2);
      gfmulNextstep128(
	&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encp - 4 * 16))),
	--ghashPrecompute);
      Ztls_AESECB6Update(3);
      _mm256_stream_si256(
	(void *)output, _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)encbuf)));
      _mm256_stream_si256(
	(void *)(output + 32),
	_mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 32))));
      Ztls_AESECB6Update(4);
      gfmulNextstep128(
	&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encp - 3 * 16))),
	--ghashPrecompute);
      Ztls_AESECB6Update(5);
      gfmulNextstep128(
	&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encp - 2 * 16))),
	--ghashPrecompute);
      Ztls_AESECB6Update(6);
      gfmulNextstep128(
	&gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encp - 1 * 16))),
	--ghashPrecompute);
      Ztls_AESECB6Update(7);
      if ((state & Ztls_StateCopy128b) != 0) {
	_mm256_stream_si256(
	  (void *)(output + 64),
	  _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 64))));
	_mm256_stream_si256(
	  (void *)(output + 96),
	  _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 96))));
	output += 128;
	encp -= 128;
	Ztls_AESECB6Update(8);
	_mm256_store_si256(
	  reinterpret_cast<__m256i *>((void *)encbuf),
	  _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 128))));
	_mm256_store_si256(
	  reinterpret_cast<__m256i *>((void *)(encbuf + 32)),
	  _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 160))));
      } else {
	output += 64;
	encp -= 64;
	_mm256_store_si256(
	  reinterpret_cast<__m256i *>((void *)encbuf),
	  _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 64))));
	_mm256_store_si256(
	  reinterpret_cast<__m256i *>((void *)(encbuf + 32)),
	  _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 96))));
	Ztls_AESECB6Update(8);
      }
      state ^= Ztls_StateCopy128b;
      Ztls_AESECB6Update(9);
      if (ZuUnlikely(ctx->ecb.rounds != 10)) {
	for (size_t i = 10; ZuLikely(i < ctx->ecb.rounds); ++i) Ztls_AESECB6Update(i);
      }
      assert(ctx->ghash.data() == ghashPrecompute);
      gfmulReduce128(&gstate);
      Ztls_AESECB6Final(ctx->ecb.rounds);
    }
  }

  // Now, All the encrypted bits are built in encbuf. Calculate AEAD tag and append to encbuf.

  {
    // Run ghash against the remaining bytes, after appending `ac` (i.e., len(A) | len(C)). At
    // this point, we might be ghashing 7 blocks at once.
    size_t acOff = remainingGhashFrom + ((encp - encbuf) - remainingGhashFrom + 15)/16 * 16;
    _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)(encbuf + acOff)), ac);
    size_t blocks = ((encp - encbuf) - remainingGhashFrom + 15)/16 + 1;
    // round up, +1 for AC
    assert(blocks <= 7);
    struct FusionGhash128 *ghashPrecompute = ctx->ghash.data() + blocks;
    gfmulFirststep128(
      &gstate,
      _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encbuf + remainingGhashFrom))),
      --ghashPrecompute);
    remainingGhashFrom += 16;
    while (ghashPrecompute != ctx->ghash.data()) {
      gfmulNextstep128(
	&gstate,
	_mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)(encbuf + remainingGhashFrom))),
	--ghashPrecompute);
      remainingGhashFrom += 16;
    }
    gfmulReduce128(&gstate);
  }

  // Calculate EK0, if in the unlikely case on not been done yet. When encoding in full size
  // (16K), EK0 will be ready.
  if (ZuUnlikely((state & Ztls_StateEk0Ready) == 0)) {
    bits5 = _mm_xor_si128(ek0, ctx->ecb.keys.m128[0]);
    for (size_t i = 1; i < ctx->ecb.rounds; ++i)
      bits5 = _mm_aesenc_si128(bits5, ctx->ecb.keys.m128[i]);
    bits5 = _mm_aesenclast_si128(bits5, ctx->ecb.keys.m128[ctx->ecb.rounds]);
  }

  // append tag to encbuf
  _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)encp), gfmulGetTag128(&gstate, bits5));
  encp += 16;

  // write remaining bytes
  writeRemainingBytes(output, encbuf, encp);

#undef Ztls_AESECB6Init
#undef Ztls_AESECB6Update
#undef Ztls_AESECB6Final
#undef Ztls_StateEk0Ready
#undef Ztls_StateCopy128b
}

static size_t nonTemporalDecrypt128(
  AeadContext *_ctx, void *_output, const void *_input, size_t inlen, uint64_t seq,
  const void *aad, size_t aadlen)
{
  // Bail out if the input is too short, or remove tag from range.
  if (inlen < 16) return SIZE_MAX;
  inlen -= 16;
  size_t textlen = inlen;

// init the bits (we can always run in full), but use the last slot for calculating ek0, if
// possible
#define Ztls_AESECB6Init() \
  do { \
    ctr = _mm_add_epi64(ctr, one8); \
    bits0 = _mm_shuffle_epi8(ctr, byteswap128); \
    ctr = _mm_add_epi64(ctr, one8); \
    bits1 = _mm_shuffle_epi8(ctr, byteswap128); \
    ctr = _mm_add_epi64(ctr, one8); \
    bits2 = _mm_shuffle_epi8(ctr, byteswap128); \
    ctr = _mm_add_epi64(ctr, one8); \
    bits3 = _mm_shuffle_epi8(ctr, byteswap128); \
    ctr = _mm_add_epi64(ctr, one8); \
    bits4 = _mm_shuffle_epi8(ctr, byteswap128); \
    if (ZuLikely(inlen > 16 * 5)) { \
      ctr = _mm_add_epi64(ctr, one8); \
      bits5 = _mm_shuffle_epi8(ctr, byteswap128); \
    } else { \
      bits5 = ek0; \
      state |= Ztls_StateEk0Ready; \
    } \
    __m128i k = ctx->ecb.keys.m128[0]; \
    bits0 = _mm_xor_si128(bits0, k); \
    bits1 = _mm_xor_si128(bits1, k); \
    bits2 = _mm_xor_si128(bits2, k); \
    bits3 = _mm_xor_si128(bits3, k); \
    bits4 = _mm_xor_si128(bits4, k); \
    bits5 = _mm_xor_si128(bits5, k); \
  } while (0)

// aes block update
#define Ztls_AESECB6Update(i) \
  do { \
    __m128i k = ctx->ecb.keys.m128[i]; \
    bits0 = _mm_aesenc_si128(bits0, k); \
    bits1 = _mm_aesenc_si128(bits1, k); \
    bits2 = _mm_aesenc_si128(bits2, k); \
    bits3 = _mm_aesenc_si128(bits3, k); \
    bits4 = _mm_aesenc_si128(bits4, k); \
    bits5 = _mm_aesenc_si128(bits5, k); \
  } while (0)

// aesenclast
#define Ztls_AESECB6Final(i) \
  do { \
    __m128i k = ctx->ecb.keys.m128[i]; \
    bits0 = _mm_aesenclast_si128(bits0, k); \
    bits1 = _mm_aesenclast_si128(bits1, k); \
    bits2 = _mm_aesenclast_si128(bits2, k); \
    bits3 = _mm_aesenclast_si128(bits3, k); \
    bits4 = _mm_aesenclast_si128(bits4, k); \
    bits5 = _mm_aesenclast_si128(bits5, k); \
  } while (0)

  struct FusionAeadContext *agctx = static_cast<FusionAeadContext *>(_ctx);
  uint8_t		   *output = static_cast<uint8_t *>(_output);
  const uint8_t		   *input = static_cast<const uint8_t *>(_input);

#define Ztls_StateEk0Ready 0x1
  int32_t state = 0;

  // setup ctr, retain Ek(0), len(A) | len(C) to be fed into GCM
  __m128i ctr = calcCounter(agctx, seq);
  ctr = _mm_insert_epi32(ctr, 1, 0);
  __m128i ek0 = _mm_shuffle_epi8(ctr, byteswap128);
  __m128i ac =
    _mm_shuffle_epi8(_mm_set_epi32(0, (int)aadlen * 8, 0, (int)inlen * 8), byteswap128);

  struct FusionGcm128  *ctx = static_cast<struct FusionGcm128 *>(agctx->aesgcm);
  __m128i		bits0, bits1, bits2, bits3, bits4, bits5 = _mm_setzero_si128();
  struct FusionGfmul128 gstate = {0};

  // Prepare first 6 blocks of bit stream, at the same time calculating ghash of AAD.
  Ztls_AESECB6Init();
  Ztls_AESECB6Update(1);
  Ztls_AESECB6Update(2);
  reduceAad128(&gstate, ctx->ghash.data(), aad, aadlen);
  for (size_t i = 3; i < ctx->ecb.rounds; ++i) Ztls_AESECB6Update(i);
  Ztls_AESECB6Final(ctx->ecb.rounds);

  // Main loop. Operate in full blocks (6 * 16 bytes).
  while (ZuLikely(inlen >= 6 * 16)) {
#define Ztls_Decrypt(i) \
  _mm_storeu_si128( \
    reinterpret_cast<__m128i *>(output + i * 16), \
    _mm_xor_si128(bits##i, _mm_loadu_si128(reinterpret_cast<const __m128i *>(input + i * 16))))
    Ztls_Decrypt(0);
    Ztls_Decrypt(1);
    Ztls_Decrypt(2);
    Ztls_Decrypt(3);
    Ztls_Decrypt(4);
    Ztls_Decrypt(5);
#undef Ztls_Decrypt
#define Ztls_GfmulNext(i) \
  gfmulNextstep128( \
    &gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>(input + i * 16)), \
    ctx->ghash.data() + 5 - i)
    Ztls_AESECB6Init();
    Ztls_AESECB6Update(1);
    Ztls_AESECB6Update(2);
    Ztls_AESECB6Update(3);
    gfmulFirststep128(
      &gstate, _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)input)),
      ctx->ghash.data() + 5);
    Ztls_AESECB6Update(4);
    Ztls_GfmulNext(1);
    Ztls_AESECB6Update(5);
    Ztls_GfmulNext(2);
    Ztls_AESECB6Update(6);
    Ztls_GfmulNext(3);
    Ztls_AESECB6Update(7);
    Ztls_GfmulNext(4);
    Ztls_AESECB6Update(8);
    Ztls_GfmulNext(5);
    Ztls_AESECB6Update(9);
    gfmulReduce128(&gstate);
    if (ZuUnlikely(ctx->ecb.rounds != 10)) {
      size_t i = 10;
      do {
	Ztls_AESECB6Update(i);
      } while (++i < ctx->ecb.rounds);
    }
    Ztls_AESECB6Final(ctx->ecb.rounds);
    output += 6 * 16;
    input += 6 * 16;
    inlen -= 6 * 16;
#undef Ztls_GfmulNext
  }

  // Decrypt the remainder as well as finishing GHASH calculation.
  if (inlen != 0) {
    struct FusionGhash128 *ghashPrecompute = ctx->ghash.data() + (inlen + 15)/16 + 1;
#define Ztls_Oneblock(i) \
  do { \
    if (inlen != 0) { \
      __m128i b = inlen >= 16 ? _mm_loadu_si128(reinterpret_cast<const __m128i *>(input)) : \
				loadn128(input, inlen); \
      if (i == 0) { \
	gfmulFirststep128(&gstate, b, --ghashPrecompute); \
      } else { \
	gfmulNextstep128(&gstate, b, --ghashPrecompute); \
      } \
      b = _mm_xor_si128(b, bits##i); \
      if (inlen >= 16) { \
	_mm_storeu_si128(reinterpret_cast<__m128i *>(output), b); \
	output += 16; \
	input += 16; \
	inlen -= 16; \
      } else { \
	storen128(output, inlen, b); \
	output += inlen; \
	input += inlen; \
	inlen = 0; \
      } \
    } \
  } while (0)
    Ztls_Oneblock(0);
    Ztls_Oneblock(1);
    Ztls_Oneblock(2);
    Ztls_Oneblock(3);
    Ztls_Oneblock(4);
    Ztls_Oneblock(5);
#undef Ztls_Oneblock
    gfmulNextstep128(&gstate, ac, --ghashPrecompute);
    assert(ghashPrecompute == ctx->ghash.data());
  } else {
    gfmulFirststep128(&gstate, ac, ctx->ghash.data());
  }
  gfmulReduce128(&gstate);

  // Calculate EK0 if not yet available in bits5.
  if ((state & Ztls_StateEk0Ready) == 0) {
    bits5 = _mm_xor_si128(ek0, ctx->ecb.keys.m128[0]);
    for (size_t i = 1; i < ctx->ecb.rounds; ++i)
      bits5 = _mm_aesenc_si128(bits5, ctx->ecb.keys.m128[i]);
    bits5 = _mm_aesenclast_si128(bits5, ctx->ecb.keys.m128[ctx->ecb.rounds]);
  }

  // Calculate GCM tag and compare.
  __m128i calctag = gfmulGetTag128(&gstate, bits5);
  __m128i recvtag = _mm_loadu_si128(reinterpret_cast<const __m128i *>((void *)input));
  if (_mm_movemask_epi8(_mm_cmpeq_epi8(calctag, recvtag)) != 0xffff) return SIZE_MAX;

  return textlen;

#undef Ztls_AESECB6Init
#undef Ztls_AESECB6Update
#undef Ztls_AESECB6Final
#undef Ztls_StateEk0Ready
}

Ztls_NoSanitizeAddress static void nonTemporalEncryptV256(
  AeadContext *_ctx, void *_output, const Bytes *input, size_t incnt, uint64_t seq,
  const void *_aad, size_t aadlen)
{
// init the bits (we can always run in full), but use the last slot for calculating ek0, if
// possible
#define Ztls_AESECB6Init() \
  do { \
    ctr = _mm256_add_epi64(ctr, incr128x2); \
    bits0 = _mm256_shuffle_epi8(ctr, byteswap256); \
    ctr = _mm256_add_epi64(ctr, incr128x2); \
    bits1 = _mm256_shuffle_epi8(ctr, byteswap256); \
    ctr = _mm256_add_epi64(ctr, incr128x2); \
    bits2 = _mm256_shuffle_epi8(ctr, byteswap256); \
    ctr = _mm256_add_epi64(ctr, incr128x2); \
    bits3 = _mm256_shuffle_epi8(ctr, byteswap256); \
    ctr = _mm256_add_epi64(ctr, incr128x2); \
    bits4 = _mm256_shuffle_epi8(ctr, byteswap256); \
    ctr = _mm256_add_epi64(ctr, incr128x2); \
    bits5 = _mm256_shuffle_epi8(ctr, byteswap256); \
    if (ZuUnlikely(srclen <= 32 * 6 - 16) && srcVecleft == 0) { \
      bits5 = _mm256_permute2f128_si256(bits5, acEk0, 0x30); \
      state |= Ztls_StateEk0Ready; \
    } \
    __m256i k = ctx->ecb.keys.m256[0]; \
    bits0 = _mm256_xor_si256(bits0, k); \
    bits1 = _mm256_xor_si256(bits1, k); \
    bits2 = _mm256_xor_si256(bits2, k); \
    bits3 = _mm256_xor_si256(bits3, k); \
    bits4 = _mm256_xor_si256(bits4, k); \
    bits5 = _mm256_xor_si256(bits5, k); \
  } while (0)

// aes block update
#define Ztls_AESECB6Update(i) \
  do { \
    __m256i k = ctx->ecb.keys.m256[i]; \
    bits0 = _mm256_aesenc_epi128(bits0, k); \
    bits1 = _mm256_aesenc_epi128(bits1, k); \
    bits2 = _mm256_aesenc_epi128(bits2, k); \
    bits3 = _mm256_aesenc_epi128(bits3, k); \
    bits4 = _mm256_aesenc_epi128(bits4, k); \
    bits5 = _mm256_aesenc_epi128(bits5, k); \
  } while (0)

// aesenclast
#define Ztls_AESECB6Final(i) \
  do { \
    __m256i k = ctx->ecb.keys.m256[i]; \
    bits0 = _mm256_aesenclast_epi128(bits0, k); \
    bits1 = _mm256_aesenclast_epi128(bits1, k); \
    bits2 = _mm256_aesenclast_epi128(bits2, k); \
    bits3 = _mm256_aesenclast_epi128(bits3, k); \
    bits4 = _mm256_aesenclast_epi128(bits4, k); \
    bits5 = _mm256_aesenclast_epi128(bits5, k); \
  } while (0)

  struct FusionAeadContext *agctx = static_cast<FusionAeadContext *>(_ctx);
  uint8_t		   *output = static_cast<uint8_t *>(_output);
  const uint8_t		   *aad = static_cast<const uint8_t *>(_aad);

#define Ztls_StateEk0Ready 0x1
  int32_t state = 0;

  // Bytes are written here first then written using NT store instructions, 64 bytes at a time.
  uint8_t encbuf[32 * 9] __attribute__((aligned(32))), *encp;

  // `encbuf` should be large enough to store up to 63-bytes of unaligned bytes, 6 16-byte AES
  // blocks, plus AEAD tag that is append to the ciphertext before writing the bytes to main
  // memory using NT store instructions.
  ZuAssert(sizeof(encbuf) >= 64 + 6 * 32 + 16);

  // load unaligned data within same cache line preceding `output`, adjusting pointers accordingly
  encp = loadPrecedingUnaligned(encbuf, &output);

  // setup ctr, retaining Ek(0), len(A) | len(C) to be fed into GCM
  size_t  totalLen = aeadVecSize(input, incnt);
  __m256i ctr = _mm256_broadcastsi128_si256(calcCounter(agctx, seq));
  ctr = _mm256_insert_epi32(ctr, 1, 4);
  __m256i acEk0 = _mm256_permute2f128_si256(
    // first half: ac
    _mm256_castsi128_si256(
      _mm_shuffle_epi8(_mm_set_epi32(0, (int)aadlen * 8, 0, (int)totalLen * 8), byteswap128)),
    // second half: ek0
    _mm256_shuffle_epi8(ctr, byteswap256), 0x30);

  struct FusionGcm256  *ctx = static_cast<struct FusionGcm256 *>(agctx->aesgcm);
  __m256i		bits0, bits1, bits2, bits3, bits4, bits5 = _mm256_setzero_si256();
  struct FusionGfmul256 gstate = {0};

  // find the first non-empty vec
  const uint8_t *src = NULL;
  size_t	 srclen = 0, srcVecleft = incnt;
  while (srclen == 0 && srcVecleft != 0) {
    src = reinterpret_cast<const uint8_t *>(input[0].data());
    srclen = input[0].length();
    ++input;
    --srcVecleft;
  }

  // Prepare first 6 blocks of bit stream, at the same time calculating ghash of AAD.
  Ztls_AESECB6Init();
  Ztls_AESECB6Update(1);
  Ztls_AESECB6Update(2);
  if (ZuLikely(aadlen != 0)) {
    union FusionGhash256 *ghashPrecompute;
    while (ZuUnlikely(aadlen >= 6 * 32)) {
      ghashPrecompute = ctx->ghash.data() + 6;
      gfmulFirststep256(
	&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)aad)), 0,
	--ghashPrecompute);
      aad += 32;
      aadlen -= 32;
      for (int i = 1; i < 6; ++i) {
	gfmulNextstep256(
	  &gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)aad)),
	  --ghashPrecompute);
	aad += 32;
	aadlen -= 32;
      }
      gfmulReduce256(&gstate);
    }
    if (ZuLikely(aadlen != 0)) {
      ghashPrecompute = ctx->ghash.data() + (aadlen + 31)/32;
      if (ZuUnlikely(aadlen >= 32)) {
	if (aadlen % 32 == 0 || aadlen % 32 > 16) {
	  gfmulFirststep256(
	    &gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)aad)), 0,
	    --ghashPrecompute);
	  aad += 32;
	  aadlen -= 32;
	} else {
	  gfmulFirststep256(
	    &gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)aad)), 1,
	    --ghashPrecompute);
	  aad += 16;
	  aadlen -= 16;
	}
	while (aadlen >= 32) {
	  gfmulNextstep256(
	    &gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)aad)),
	    --ghashPrecompute);
	  aad += 32;
	  aadlen -= 32;
	}
	if (ZuLikely(aadlen != 0)) {
	  assert(aadlen > 16);
	  gfmulNextstep256(&gstate, loadn256(aad, aadlen), --ghashPrecompute);
	}
      } else {
	gfmulFirststep256(&gstate, loadn256(aad, aadlen), aadlen <= 16, --ghashPrecompute);
      }
      assert(ctx->ghash.data() == ghashPrecompute);
      gfmulReduce256(&gstate);
    }
  }
  for (size_t i = 3; i < ctx->ecb.rounds; ++i) Ztls_AESECB6Update(i);
  Ztls_AESECB6Final(ctx->ecb.rounds);

  //  Main loop. This loop:
  //  1. using current keystream (bits0..bits5), xors a up to 6 * 16 bytes and writes to encbuf,
  //  2. then if there is no more data to be encrypted, exit the loop, otherwise,
  //  3. calculate ghash of the blocks being written to encbuf,
  //  4. calculate next 6 * 16 bytes of keystream,
  //  5. writes encbuf in 64-byte blocks
  // When exitting the loop, `remainingGhashFrom` represents the offset within `encbuf` from
  // where ghash remains to be
  // calculated.
  size_t remainingGhashFrom = encp - encbuf;
  if (srclen != 0) {
    while (1) {
      // apply the bit stream to input, writing to encbuf
      if (ZuLikely(srclen >= 6 * 32)) {
#define Ztls_Apply(i) \
  \
  _mm256_storeu_si256( \
    reinterpret_cast<__m256i *>(encp + i * 32), \
    _mm256_xor_si256( \
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + i * 32)), bits##i))
	Ztls_Apply(0);
	Ztls_Apply(1);
	Ztls_Apply(2);
	Ztls_Apply(3);
	Ztls_Apply(4);
	Ztls_Apply(5);
#undef Ztls_Apply
	encp += 6 * 32;
	src += 6 * 32;
	srclen -= 6 * 32;
	if (ZuUnlikely(srclen == 0)) {
	  if (srcVecleft == 0) {
	    remainingGhashFrom = (encp - encbuf) - 6 * 32;
	    break;
	  }
	  src = reinterpret_cast<const uint8_t *>(input[0].data());
	  srclen = input[0].length();
	  ++input;
	  --srcVecleft;
	}
      } else {
	// slow path, load at most 6 * 32 bytes to encbuf then encrypt in-place
	size_t bytesCopied = 0;
	do {
	  if (srclen >= 32 && bytesCopied < 5 * 32) {
	    _mm256_storeu_si256(
	      reinterpret_cast<__m256i *>((void *)(encp + bytesCopied)),
	      _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)src)));
	    bytesCopied += 32;
	    src += 32;
	    srclen -= 32;
	  } else {
	    encp[bytesCopied++] = *src++;
	    --srclen;
	  }
	  if (ZuUnlikely(srclen == 0)) {
	    do {
	      if (srcVecleft == 0) break;
	      src = reinterpret_cast<const uint8_t *>(input[0].data());
	      srclen = input[0].length();
	      ++input;
	      --srcVecleft;
	    } while (srclen == 0);
	    if (srclen == 0) break;
	  }
	} while (bytesCopied < 6 * 32);
#define Ztls_Apply(i) \
  \
  _mm256_storeu_si256( \
    reinterpret_cast<__m256i *>(encp + i * 32), \
    _mm256_xor_si256( \
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(encp + i * 32)), bits##i))
	Ztls_Apply(0);
	Ztls_Apply(1);
	Ztls_Apply(2);
	Ztls_Apply(3);
	Ztls_Apply(4);
	Ztls_Apply(5);
#undef Ztls_Apply
	encp += bytesCopied;
	if (ZuUnlikely(srclen == 0)) {
	  // Calculate amonut of data left to be ghashed, as well as zero-clearing the remainedr
	  // of partial block, as it will be fed into ghash.
	  remainingGhashFrom = (encp - encbuf) - bytesCopied;
	  if ((bytesCopied & 15) != 0)
	    _mm_storeu_si128(reinterpret_cast<__m128i *>((void *)encp), _mm_setzero_si128());
	  break;
	}
      }

      // Next 96-byte block starts here. Run AES and ghash in parallel while writing output using
      // non-temporal store instructions.
      Ztls_AESECB6Init();
      union FusionGhash256 *ghashPrecompute = ctx->ghash.data() + 6;
      gfmulFirststep256(
	&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encp - 6 * 32))),
	0, --ghashPrecompute);
      Ztls_AESECB6Update(1);
      gfmulNextstep256(
	&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encp - 5 * 32))),
	--ghashPrecompute);
      Ztls_AESECB6Update(2);
      gfmulNextstep256(
	&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encp - 4 * 32))),
	--ghashPrecompute);
      Ztls_AESECB6Update(3);
      gfmulNextstep256(
	&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encp - 3 * 32))),
	--ghashPrecompute);
      Ztls_AESECB6Update(4);
      _mm256_stream_si256(
	(void *)output, _mm256_load_si256(reinterpret_cast<const __m256i *>((void *)encbuf)));
      _mm256_stream_si256(
	(void *)(output + 32),
	_mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 32))));
      _mm256_stream_si256(
	(void *)(output + 64),
	_mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 64))));
      _mm256_stream_si256(
	(void *)(output + 96),
	_mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 96))));
      _mm256_stream_si256(
	(void *)(output + 128),
	_mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 128))));
      _mm256_stream_si256(
	(void *)(output + 160),
	_mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 160))));
      Ztls_AESECB6Update(5);
      gfmulNextstep256(
	&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encp - 2 * 32))),
	--ghashPrecompute);
      Ztls_AESECB6Update(6);
      gfmulNextstep256(
	&gstate, _mm256_loadu_si256(reinterpret_cast<const __m256i *>((void *)(encp - 1 * 32))),
	--ghashPrecompute);
      output += 192;
      encp -= 192;
      Ztls_AESECB6Update(7);
      _mm256_store_si256(
	reinterpret_cast<__m256i *>((void *)encbuf),
	_mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 192))));
      Ztls_AESECB6Update(8);
      _mm256_store_si256(
	reinterpret_cast<__m256i *>((void *)(encbuf + 32)),
	_mm256_load_si256(reinterpret_cast<const __m256i *>((void *)(encbuf + 224))));
      Ztls_AESECB6Update(9);
      if (ZuUnlikely(ctx->ecb.rounds != 10)) {
	for (size_t i = 10; ZuLikely(i < ctx->ecb.rounds); ++i) Ztls_AESECB6Update(i);
      }
      assert(ctx->ghash.data() == ghashPrecompute);
      gfmulReduce256(&gstate);
      Ztls_AESECB6Final(ctx->ecb.rounds);
    }
  }

  // Now, All the encrypted bits are built in encbuf. Calculate AEAD tag and append to encbuf.

  {
    // Run ghash against the remaining bytes, after appending `ac` (i.e., len(A) | len(C)). At
    // this point, we might be ghashing 7 blocks at once.
    size_t acOff = remainingGhashFrom + ((encp - encbuf) - remainingGhashFrom + 15)/16 * 16;
    _mm_storeu_si128(
      reinterpret_cast<__m128i *>((void *)(encbuf + acOff)), _mm256_castsi256_si128(acEk0));
    size_t blocks = ((encp - encbuf) - remainingGhashFrom + 15)/16 + 1;
    // round up, +1 for AC
    assert(blocks <= 13);
    union FusionGhash256 *ghashPrecompute = ctx->ghash.data() + blocks/2;
    if (blocks % 2 != 0) {
      gfmulFirststep256(
	&gstate,
	_mm256_loadu_si256(
	  reinterpret_cast<const __m256i *>((void *)(encbuf + remainingGhashFrom))),
	1, ghashPrecompute);
      remainingGhashFrom += 16;
    } else {
      gfmulFirststep256(
	&gstate,
	_mm256_loadu_si256(
	  reinterpret_cast<const __m256i *>((void *)(encbuf + remainingGhashFrom))),
	0, --ghashPrecompute);
      remainingGhashFrom += 32;
    }
    while (ghashPrecompute != ctx->ghash.data()) {
      gfmulNextstep256(
	&gstate,
	_mm256_loadu_si256(
	  reinterpret_cast<const __m256i *>((void *)(encbuf + remainingGhashFrom))),
	--ghashPrecompute);
      remainingGhashFrom += 32;
    }
    gfmulReduce256(&gstate);
  }

  // Calculate EK0, if in the unlikely case on not been done yet. When encoding in full size
  // (16K), EK0 will be ready.
  if (ZuUnlikely((state & Ztls_StateEk0Ready) == 0)) {
    bits5 = acEk0;
    bits5 = _mm256_xor_si256(bits5, ctx->ecb.keys.m256[0]);
    for (size_t i = 1; i < ctx->ecb.rounds; ++i)
      bits5 = _mm256_aesenc_epi128(bits5, ctx->ecb.keys.m256[i]);
    bits5 = _mm256_aesenclast_epi128(bits5, ctx->ecb.keys.m256[ctx->ecb.rounds]);
  }

  // append tag to encbuf
  _mm_storeu_si128(
    reinterpret_cast<__m128i *>((void *)encp),
    gfmulGetTag256(
      &gstate, _mm256_castsi256_si128(_mm256_permute2f128_si256(bits5, bits5, 0x11))));
  encp += 16;

  // write remaining bytes
  writeRemainingBytes(output, encbuf, encp);
}

static int
nonTemporalSetup(AeadContext *_ctx, int isEnc, const void *key, const void *iv, size_t keySize)
{
  struct FusionAeadContext *ctx = static_cast<FusionAeadContext *>(_ctx);
  int			    aesni256 = isEnc && fusionCanAesni256;

  memset(&ctx->serial, 0, sizeof(ctx->serial));

  ctx->staticIv = loadn128(iv, Ztls_AESGCMIVSize);
  ctx->staticIv = _mm_shuffle_epi8(ctx->staticIv, byteswap128);
  if (key == NULL) return 0;

  assert(isEnc || !aesni256);

  ctx->aesgcm = fusionAesgcmNew(
    key, keySize,
    7 *
      (fusionCanAesni256 ? 32 :
			   16), // 6 blocks at once, plus len(A) | len(C) that we might append
    aesni256);
  return 0;
}

static const FusionAeadAlgo nonTemporalAes128gcmAlgo{"AES128-GCM", Ztls_AESGCMConfLimit,
  Ztls_AESGCMIntegrityLimit, &fusionAes128ctr,
  NULL, // &fusion_aes128ecb,
  Ztls_AES128KeySize, Ztls_AESGCMIVSize, Ztls_AESGCMTagSize,

  1, ZtlsX86CacheLineAlignBits,

  1, 1};
const AeadAlgo		   &nonTemporalAes128gcm = nonTemporalAes128gcmAlgo;
static const FusionAeadAlgo nonTemporalAes256gcmAlgo{"AES256-GCM", Ztls_AESGCMConfLimit,
  Ztls_AESGCMIntegrityLimit, &fusionAes256ctr,
  NULL, // &fusion_aes128ecb,
  Ztls_AES256KeySize, Ztls_AESGCMIVSize, Ztls_AESGCMTagSize,

  1, ZtlsX86CacheLineAlignBits,

  1, 1};
const AeadAlgo		   &nonTemporalAes256gcm = nonTemporalAes256gcmAlgo;

#ifdef _WIN32

// fusionSupported:
// Check that the CPU has extended instructions for PCMUL, AES and AVX2.
// This test assumes that the CPU is following the x86/x64 architecture.
// A slightly more refined test could check that the cpuInfo spells out
// "genuineIntel" or "authenticAMD", but would fail in presence of
// little known CPU brands or some VM
int fusionSupported(void)
{
  uint32_t cpuInfo[4];
  uint32_t nbIds;
  int	   isSupported = 0;

  __cpuid(cpuInfo, 0);
  nbIds = cpuInfo[0];

  if (nbIds >= 7) {
    uint32_t leaf1Ecx;
    __cpuid(cpuInfo, 1);
    leaf1Ecx = cpuInfo[2];

    if (
      // PCLMUL
      (leaf1Ecx & (1 << 5)) != 0 &&
      // AES
      (leaf1Ecx & (1 << 25)) != 0) {
      uint32_t leaf7Ebx, leaf7Ecx;
      __cpuid(cpuInfo, 7);
      leaf7Ebx = cpuInfo[1];
      leaf7Ecx = cpuInfo[2];

      isSupported =
	// AVX2
	(leaf7Ebx & (1 << 5)) != 0;

      // enable 256-bit mode if possible
      if (isSupported && (leaf7Ecx & 0x600) != 0 && !fusionCanAesni256) fusionCanAesni256 = 1;
    }
  }

  return isSupported;
}
#else
int fusionSupported(void)
{
  unsigned leaf1Ecx, leaf7Ebx, leaf7Ecx;

  {
    // GCC-specific code to obtain CPU features
    unsigned leafCnt;
    __asm__("cpuid" : "=a"(leafCnt) : "a"(0) : "ebx", "ecx", "edx");
    if (leafCnt < 7) return 0;
    __asm__("cpuid" : "=c"(leaf1Ecx) : "a"(1) : "ebx", "edx");
    __asm__("cpuid" : "=b"(leaf7Ebx), "=c"(leaf7Ecx) : "a"(7), "c"(0) : "edx");
  }

  // AVX2
  if ((leaf7Ebx & (1 << 5)) == 0) return 0;
  // AES
  if ((leaf1Ecx & (1 << 25)) == 0) return 0;
  // PCLMUL
  if ((leaf1Ecx & (1 << 1)) == 0) return 0;

  // enable 256-bit mode if possible
  if ((leaf7Ecx & 0x600) != 0 && !fusionCanAesni256) fusionCanAesni256 = 1;

  return 1;
}
#endif

FusionAeadContext::~FusionAeadContext()
{
  ZuClear(&serial, sizeof(serial));
  if (aesgcm) fusionAesgcmFree(aesgcm);
  ZuClear(&staticIv, sizeof(staticIv));
}

void FusionAeadContext::getIV(void *iv) { aesgcmGetIV(this, iv); }

void FusionAeadContext::setIV(const void *iv) { aesgcmSetIV(this, iv); }

int FusionAeadContext::encBegin(uint64_t seq, const void *aad, size_t aadlen)
{
  if (!isEnc) return Ztls_ErrorNotAvailable;
  return fusionAeadEncBegin(this, seq, aad, aadlen);
}

int FusionAeadContext::encChunk(void *output, const void *input, size_t length, size_t *written)
{
  if (!isEnc) {
    *written = 0;
    return Ztls_ErrorNotAvailable;
  }
  return fusionAeadEncChunk(this, output, input, length, written);
}

int FusionAeadContext::encEnd(void *output, size_t capacity, size_t *written)
{
  if (!isEnc) {
    *written = 0;
    return Ztls_ErrorNotAvailable;
  }
  return fusionAeadEncEnd(this, output, capacity, written);
}

int FusionAeadContext::decBegin(uint64_t seq, const void *aad, size_t aadlen)
{
  if (isEnc) return Ztls_ErrorNotAvailable;
  return fusionAeadDecBegin(this, seq, aad, aadlen);
}

int FusionAeadContext::decChunk(
  void *output, size_t capacity, const void *input, size_t length, size_t *written)
{
  if (isEnc) {
    *written = 0;
    return Ztls_ErrorNotAvailable;
  }
  return fusionAeadDecChunk(this, output, capacity, input, length, written);
}

int FusionAeadContext::decEnd(const void *tag, size_t taglen)
{
  if (isEnc) return Ztls_ErrorNotAvailable;
  return fusionAeadDecEnd(this, tag, taglen);
}

void FusionAeadContext::decAbort() { fusionAeadDecAbort(this); }

void FusionAeadContext::encryptV(
  void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad, size_t aadlen)
{
  assert(isEnc);
  if (!algo->nonTemporal)
    aeadDoEncryptV(this, output, input, incnt, seq, aad, aadlen);
  else if (aesgcm->ecb.aesni256)
    nonTemporalEncryptV256(this, output, input, incnt, seq, aad, aadlen);
  else
    nonTemporalEncryptV128(this, output, input, incnt, seq, aad, aadlen);
}

size_t FusionAeadContext::decrypt(
  void *output, const void *input, size_t inlen, uint64_t seq, const void *aad, size_t aadlen)
{
  assert(!isEnc);
  return algo->nonTemporal ? nonTemporalDecrypt128(this, output, input, inlen, seq, aad, aadlen) :
			     aeadDoDecrypt(this, output, input, inlen, seq, aad, aadlen);
}

void FusionAeadContext::encrypt(
  void *output, const void *input, size_t inlen, uint64_t seq, const void *aad, size_t aadlen,
  AeadSupplement *supp)
{
  assert(isEnc);
  if (algo->nonTemporal)
    AeadContext::encrypt(output, input, inlen, seq, aad, aadlen, supp);
  else
    aeadDoEncrypt(this, output, input, inlen, seq, aad, aadlen, supp);
}

void FusionAeadContext::encryptVS(
  void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad, size_t aadlen,
  AeadSupplement *supp)
{
  assert(isEnc);
  if (algo->nonTemporal)
    AeadContext::encryptVS(output, input, incnt, seq, aad, aadlen, supp);
  else
    aeadDoEncryptVS(this, output, input, incnt, seq, aad, aadlen, supp);
}

} // namespace Ztls_

#endif
