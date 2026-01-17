//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// picotls buffer hook integration

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

struct PreservePrefix {
  ZiIOBuf	*buf = nullptr;
  uint32_t	skip = 0;
  uint32_t	length = 0;

  PreservePrefix(ZiIOBuf *buf_, uint32_t off) :
      buf{buf_}, skip{buf_->skip}, length{buf_->length} {
    buf->skip = 0;
    buf->length = off;
  }
  ~PreservePrefix() {
    buf->skip = skip;
    buf->length = length;
  }
};

static void *buffer_alloc_(ptls_buffer_t *buf, uint32_t capacity,
    uint8_t align_bits)
{
  if (buf->origin) {
    if (ZuUnlikely(align_bits > IOBufAlignBits)) {
      counters.origin_align_fail++;
      return nullptr;
    }
    auto zbuf = static_cast<ZiIOBuf *>(buf->origin);
    PreservePrefix preserve{zbuf, buf->off};
    if (ZuUnlikely(!zbuf->ensure(capacity))) {
      counters.origin_ensure_fail++;
      return nullptr;
    }
    buf->base = zbuf->data() - zbuf->skip;
    buf->capacity = zbuf->size;
    buf->is_allocated = 1;
    buf->align_bits = align_bits;
    counters.origin_alloc++;
    return buf->base;
  }

  auto newp = static_cast<uint8_t *>(Zi::VHeap::valloc(capacity));
  if (ZuUnlikely(!newp)) {
    counters.internal_alloc_fail++;
    return nullptr;
  }
  if (buf->off) memcpy(newp, buf->base, buf->off);
  ptls_clear_memory(buf->base, buf->off);
  if (buf->is_allocated) Zi::VHeap::vfree(buf->base);
  buf->base = newp;
  buf->capacity = capacity;
  buf->is_allocated = 1;
  buf->align_bits = align_bits;
  counters.internal_alloc++;
  return newp;
}

static void buffer_free_(ptls_buffer_t *buf)
{
  if (ZuUnlikely(!buf || !buf->base)) return;
  if (buf->origin) {
    counters.origin_free++;
    return;
  }
  Zi::VHeap::vfree(buf->base);
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
  counters.origin_alloc.store_(0);
  counters.internal_alloc.store_(0);
  counters.origin_free.store_(0);
  counters.internal_free.store_(0);
  counters.origin_align_fail.store_(0);
  counters.origin_ensure_fail.store_(0);
  counters.internal_alloc_fail.store_(0);
}

} // namespace Ztls::Pico
