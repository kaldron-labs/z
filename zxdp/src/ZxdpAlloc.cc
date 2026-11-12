//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmAssert.hh>
#include <zlib/ZxdpAlloc.hh>
#include <zlib/ZxdpUMem.hh>

namespace Zxdp {

BufPool::BufPool(unsigned n, unsigned bytes) {
  count = n;
  size = bytes;
}
BufPool::~BufPool() { ZmAssert_(!unsigned(m_used)); }

void BufPool::init(ZmRef<UMem> arena, uint64_t offset, unsigned stride, unsigned prefix, unsigned first)
{
  ZmAssert_(!unsigned(m_used));
  m_active = 0;
  m_arena = ZuMv(arena);
  m_offset = offset;
  m_shift = __builtin_ctz(stride);
  m_prefix = prefix;
  m_first = first;
  m_head = 0;
  // Same non-sharded payload-overlay slist initialization as ZmHeap arenas.
  for (unsigned i = count; i; --i) {
    auto p = backing(i - 1);
    reinterpret_cast<ZmAtomic<uintptr_t> *>(p)->store_(m_head.load_());
    m_head.store_(uintptr_t(p));
  }
  m_active = 1;
}

uint8_t *BufPool::alloc()
{
  if (ZuUnlikely(!unsigned(m_active))) return nullptr;
  uintptr_t p;
loop:
  p = m_head.load_();
  if (ZuUnlikely(!p)) return nullptr;
  if (ZuUnlikely(p & 1)) { ZmAtomic_acquire(); goto loop; }
  if (ZuUnlikely(m_head.cmpXch(p | 1, p) != p)) goto loop;
  ++m_used;
  m_head = reinterpret_cast<ZmAtomic<uintptr_t> *>(p)->load_();
  return reinterpret_cast<uint8_t *>(p);
}

void BufPool::free(void *packet)
{
  auto id = slot(packet);
  ZmAssert_(id != NoSlot);
  auto p = backing(id);
  uintptr_t next;
loop:
  next = m_head.load_();
  if (next & 1) { ZmAtomic_acquire(); goto loop; }
  reinterpret_cast<ZmAtomic<uintptr_t> *>(p)->store_(next);
  if (m_head.cmpXch(uintptr_t(p), next) != next) goto loop;
  // Final accounting follows publication; no mapping access follows it.
  ZmAssert_(m_used--);
}

unsigned BufPool::slot(const void *packet) const
{
  if (!m_arena) return NoSlot;
  auto begin = uintptr_t(m_arena->data()) + m_offset + m_prefix;
  auto address = uintptr_t(packet);
  if (address < begin) return NoSlot;
  auto offset = address - begin;
  auto id = offset>>m_shift;
  auto within = offset & (stride() - 1);
  return id < count && within < size ? unsigned(id) : NoSlot;
}
bool BufPool::owns(const void *packet) const { return slot(packet) != NoSlot; }
uint8_t *BufPool::backing(unsigned id) const {
  ZmAssert_(m_arena && id < count);
  return m_arena->data() + m_offset + (uint64_t(id)<<m_shift) + m_prefix;
}

} // namespace Zxdp
