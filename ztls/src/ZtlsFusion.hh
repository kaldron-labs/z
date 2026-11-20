//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// Copyright (c) 2020 Fastly, Kazuho Oku
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
#ifndef ZtlsFusion_HH
#define ZtlsFusion_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZtlsEngine.hh>

#if ZtlsFusion
#include <stddef.h>
#include <emmintrin.h>
#include <immintrin.h>

namespace Ztls_ {

#define Ztls_FusionAES128Rounds 10
#define Ztls_FusionAES256Rounds 14

#ifndef ZtlsX86CacheLineAlignBits
// 64-bytes
#define ZtlsX86CacheLineAlignBits 6
#endif

struct alignas(32) FusionEcbContext {
  union Keys {
    __m128i	*m128;
    __m256i	*m256;
  } keys{nullptr};
  unsigned	rounds = 0;
  uint8_t	aesni256 = 0;

  FusionEcbContext() = default;
  FusionEcbContext(const FusionEcbContext &) = delete;
  FusionEcbContext &operator=(const FusionEcbContext &) = delete;
  Ztls_API ~FusionEcbContext();
};

struct FusionGcmContext;

void fusion_aesecb_init(
  FusionEcbContext *ctx, int is_enc, const void *key, size_t keySize, int avx256);
void fusion_aesecb_dispose(FusionEcbContext *ctx);
void fusion_aesecb_encrypt(FusionEcbContext *ctx, void *dst, const void *src);

// Creates an AES-GCM context.
// @param key       the AES key (128 bits)
// @param capacity  maximum size of AEAD record (i.e. AAD + encrypted payload)
FusionGcmContext *fusion_aesgcm_new(const void *key, size_t keySize, size_t capacity);

// Updates the capacity.
FusionGcmContext *fusion_aesgcm_set_capacity(FusionGcmContext *ctx, size_t capacity);

// Destroys an AES-GCM context.
void fusion_aesgcm_free(FusionGcmContext *ctx);

// Encrypts an AEAD block, and in parallel, optionally encrypts one block using AES-ECB.
// @param ctx      context
// @param output   output buffer
// @param input    payload to be encrypted
// @param inlen    size of the payload to be encrypted
// @param counter
// @param aad      AAD
// @param aadlen   size of AAD
// @param supp     (optional) supplementary encryption context
void fusion_aesgcm_encrypt(
  FusionGcmContext *ctx, void *output, const void *input, size_t inlen, __m128i ctr,
  const void *aad, size_t aadlen, AeadSupplement *supp);

// Decrypts an AEAD block, an in parallel, optionally encrypts one block using AES-ECB. Returns if
// decryption was successful. @param iv       initialization vector of 12 bytes @param output
// output buffer @param input    payload to be decrypted @param inlen    size of the payload to be
// decrypted @param aad      AAD @param aadlen   size of AAD @param tag      the AEAD tag being
// received from peer
int fusion_aesgcm_decrypt(
  FusionGcmContext *ctx, void *output, const void *input, size_t inlen, __m128i ctr,
  const void *aad, size_t aadlen, const void *tag);

// Checked, bounded serial AES-GCM encryption. Destinations may be unaligned.
// begin initializes one AEAD operation; chunk emits exactly length bytes; end emits its tag.
// A callback error clears the serial state and makes that operation terminal.
int fusion_aead_enc_begin(AeadContext *ctx, uint64_t seq, const void *aad, size_t aadlen);
int fusion_aead_enc_chunk(
  AeadContext *ctx, void *output, const void *input, size_t length, size_t *written);
int fusion_aead_enc_end(AeadContext *ctx, void *output, size_t capacity, size_t *written);

// Serial decryption writes provisional plaintext into the checked destination.
// Only a successful end authenticates it; abort clears an unfinished operation.
int fusion_aead_dec_begin(AeadContext *ctx, uint64_t seq, const void *aad, size_t aadlen);
int fusion_aead_dec_chunk(
  AeadContext *ctx, void *output, size_t capacity, const void *input, size_t length,
  size_t *written);
int fusion_aead_dec_end(AeadContext *ctx, const void *tag, size_t taglen);
void fusion_aead_dec_abort(AeadContext *ctx);

// A boolean flag indicating if vaes and vpclmulqdq (256-bit crypto instructions) should be used.
// This flag is set automatically
// when `fusion_is_supported_by_cpu` is called. Users can update the flag to enforce behavior.
// Engines that do not have support
// for these 256-bit instructions will continue using the 128-bit ones, even when this flag is
// set.
Ztls_Extern int fusion_can_aesni256;

// AES-CTR backends. These backends can only handle ciphertext up to 16 bytes long.
Ztls_Extern const CipherAlgo &fusion_aes128ctr, &fusion_aes256ctr;

// AES-GCM backends optimized for for QUIC. These engines require sufficient amount of memory to
// be available; they raise assertion failure if context memory cannot be allocated.
Ztls_Extern const AeadAlgo &fusion_aes128gcm, &fusion_aes256gcm;

// AES-GCM backend optimized for TLS. This backend writes ciphertext using non-temporal store
// instructions to avoid polluting CPU caches with data that is typically consumed by NIC DMA and
// not reused by the CPU. This can be beneficial on systems where DMA reads only from main memory,
// such as CPUs without Intel DDIO support. Although non-temporal stores have lower throughput
// than regular cached stores, that cost is usually hidden by AES-GCM computation.
Ztls_Extern const AeadAlgo &non_temporal_aes128gcm, &non_temporal_aes256gcm;

// Implements the 4-pass Feistel Network backed by AES-ECB, used for encrypting the Connection ID
// in draft-ietf-quic-load-balancers.

// Returns a boolean indicating if fusion can be used.
Ztls_API int fusion_is_supported_by_cpu(void);

} // namespace Ztls_

#endif // ZtlsFusion

#endif
