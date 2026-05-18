//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// picotls buffer hook integration

#include <stdint.h>
#include <string.h>

#include <zlib/ZuLib.hh>
#include <zlib/ZtlsPico.hh>

#include <zlib/ZmAtomic.hh>
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
  "ZiIOBuf_Align must satisfy picotls fusion alignment");
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
    if (ZuUnlikely(capacity > UINT32_MAX)) {
      counters.origin_ensure_fail++;
      return nullptr;
    }
    if (ZuUnlikely(!buf->ensure(static_cast<uint32_t>(capacity)))) {
      counters.origin_ensure_fail++;
      return nullptr;
    }
    pbuf->base = buf->data();
    pbuf->capacity = buf->size;
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

} // namespace Ztls::Pico
