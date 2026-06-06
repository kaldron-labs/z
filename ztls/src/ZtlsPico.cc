//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// zpicotls buffer hook integration

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <zlib/ZuLib.hh>
#include <zlib/ZtlsPico.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmVHeap.hh>
#include <zlib/ZiIOBuf.hh>

#include <zlib/ZtlsBackend.hh>

namespace Ztls::Pico {
namespace {

struct Counters {
  ZmAtomic<uint64_t> origin_alloc{0};
  ZmAtomic<uint64_t> internal_alloc{0};
  ZmAtomic<uint64_t> origin_free{0};
  ZmAtomic<uint64_t> internal_free{0};
  ZmAtomic<uint64_t> origin_align_fail{0};
  ZmAtomic<uint64_t> origin_ensure_fail{0};
  ZmAtomic<uint64_t> internal_alloc_fail{0};
};

Counters counters;

using CtxHeap = ZmVHeap<
  "Ztls.Pico.Ctx", 128, (1U<<17), ZiIOBuf_Align>;

constexpr unsigned log2_align_(unsigned value) {
  unsigned bits = 0;
  while ((1u << bits) < value) ++bits;
  return bits;
}

static_assert((ZiIOBuf_Align & (ZiIOBuf_Align - 1)) == 0,
  "ZiIOBuf_Align must be a power of two");

constexpr unsigned IOBufAlignBits = log2_align_(ZiIOBuf_Align);

#if Ztls_Fusion
static_assert(IOBufAlignBits >= PTLS_X86_CACHE_LINE_ALIGN_BITS,
  "ZiIOBuf_Align must satisfy zpicotls fusion alignment");
#endif

static void *buffer_alloc_(ptls_buffer_t *pbuf, size_t capacity,
    uint8_t align_bits, int tx)
{
  pbuf->tx = tx ? 1 : 0;
  if (pbuf->origin) {
    if (ZuUnlikely(align_bits > IOBufAlignBits)) {
      counters.origin_align_fail++;
      return nullptr;
    }
    auto buf = static_cast<ZiIOBuf *>(pbuf->origin);
    auto raw = buf->data_();
    // Preserve zpicotls' base offset across ZiIOBuf relocation.
    ptrdiff_t offset = pbuf->base ? pbuf->base - raw : 0;
    if (ZuUnlikely(offset < 0)) {
      counters.origin_ensure_fail++;
      return nullptr;
    }
    auto offset_ = size_t(offset);
    if (ZuUnlikely(capacity > UINT32_MAX ||
	  offset_ > UINT32_MAX - capacity)) {
      counters.origin_ensure_fail++;
      return nullptr;
    }
    if (ZuUnlikely(!buf->ensure(uint32_t(offset_ + capacity)))) {
      counters.origin_ensure_fail++;
      return nullptr;
    }
    raw = buf->data_();
    // Capacity remains relative to the preserved base, not data().
    pbuf->base = raw + offset_;
    pbuf->capacity = buf->size - offset_;
    pbuf->is_allocated = 1;
    pbuf->align_bits = align_bits;
    counters.origin_alloc++;
    return pbuf->base;
  }

  auto newp = static_cast<uint8_t *>(Zi::VHeap::valloc(capacity));
  if (ZuUnlikely(!newp)) {
    counters.internal_alloc_fail++;
    return nullptr;
  }
  if (pbuf->off) memcpy(newp, pbuf->base, pbuf->off);
  ptls_clear_memory(pbuf->base, pbuf->off);
  if (pbuf->is_allocated) Zi::VHeap::vfree(pbuf->base);
  pbuf->base = newp;
  pbuf->capacity = capacity;
  pbuf->is_allocated = 1;
  pbuf->align_bits = align_bits;
  counters.internal_alloc++;
  return newp;
}

static void buffer_free_(ptls_buffer_t *pbuf, int tx)
{
  (void)tx;
  if (ZuUnlikely(!pbuf || !pbuf->base)) return;
  if (pbuf->origin) {
    counters.origin_free++;
    return;
  }
  Zi::VHeap::vfree(pbuf->base);
  counters.internal_free++;
}

} // namespace

void install()
{
  static bool done = false;
  if (done) return;
  done = true;
  ptls_buffer_alloc = buffer_alloc_;
  ptls_buffer_free = buffer_free_;
}

Stats stats()
{
  return Stats{
    counters.origin_alloc.load_(),
    counters.internal_alloc.load_(),
    counters.origin_free.load_(),
    counters.internal_free.load_(),
    counters.origin_align_fail.load_(),
    counters.origin_ensure_fail.load_(),
    counters.internal_alloc_fail.load_()
  };
}

void reset_stats()
{
  counters.origin_alloc = 0;
  counters.internal_alloc = 0;
  counters.origin_free = 0;
  counters.internal_free = 0;
  counters.origin_align_fail = 0;
  counters.origin_ensure_fail = 0;
  counters.internal_alloc_fail = 0;
}

bool AeadCtx::init(
  ptls_aead_algorithm_t *algo, bool enc, const void *key, const void *iv)
{
  clear();
  if (!algo || !key || !iv ||
      algo->context_size < sizeof(ptls_aead_context_t) ||
      algo->context_size > UINT16_MAX ||
      algo->align_bits > IOBufAlignBits)
    return false;

  auto storage = CtxHeap::valloc(algo->context_size);
  if (!storage) return false;
  auto ctx = reinterpret_cast<ptls_aead_context_t *>(
    storage);
  *ctx = ptls_aead_context_t{algo};
  if (algo->setup_crypto(ctx, enc ? 1 : 0, key, iv)) {
    ptls_clear_memory(ctx, algo->context_size);
    CtxHeap::vfree(storage);
    return false;
  }
  if (enc) {
    if (!ctx->dispose_crypto || !ctx->do_encrypt || !ctx->do_encrypt_v ||
	!ctx->do_encrypt_v_s) {
      if (ctx->dispose_crypto) ctx->dispose_crypto(ctx);
      ptls_clear_memory(ctx, algo->context_size);
      CtxHeap::vfree(storage);
      return false;
    }
  } else if (!ctx->dispose_crypto || !ctx->do_decrypt) {
    if (ctx->dispose_crypto) ctx->dispose_crypto(ctx);
    ptls_clear_memory(ctx, algo->context_size);
    CtxHeap::vfree(storage);
    return false;
  }

  m_storage = storage;
  m_ctx = ctx;
  m_size = uint16_t(algo->context_size);
  return true;
}

void AeadCtx::clear()
{
  if (!m_storage) return;
  if (m_ctx) {
    m_ctx->dispose_crypto(m_ctx);
    if (m_size) ptls_clear_memory(m_ctx, m_size);
  }
  CtxHeap::vfree(m_storage);
  m_storage = nullptr;
  m_ctx = nullptr;
  m_size = 0;
}

bool CipherCtx::init(ptls_cipher_algorithm_t *algo, bool enc, const void *key)
{
  clear();
  if (!algo || !key ||
      algo->context_size < sizeof(ptls_cipher_context_t) ||
      algo->context_size > UINT16_MAX)
    return false;

  auto storage = CtxHeap::valloc(algo->context_size);
  if (!storage) return false;
  auto ctx = reinterpret_cast<ptls_cipher_context_t *>(
    storage);
  *ctx = ptls_cipher_context_t{algo};
  if (algo->setup_crypto(ctx, enc ? 1 : 0, key)) {
    ptls_clear_memory(ctx, algo->context_size);
    CtxHeap::vfree(storage);
    return false;
  }
  if (!ctx->do_init || !ctx->do_transform || !ctx->do_dispose) {
    if (ctx->do_dispose) ctx->do_dispose(ctx);
    ptls_clear_memory(ctx, algo->context_size);
    CtxHeap::vfree(storage);
    return false;
  }

  m_storage = storage;
  m_ctx = ctx;
  m_size = uint16_t(algo->context_size);
  return true;
}

void CipherCtx::clear()
{
  if (!m_storage) return;
  if (m_ctx) {
    m_ctx->do_dispose(m_ctx);
    if (m_size) ptls_clear_memory(m_ctx, m_size);
  }
  CtxHeap::vfree(m_storage);
  m_storage = nullptr;
  m_ctx = nullptr;
  m_size = 0;
}

} // namespace Ztls::Pico
