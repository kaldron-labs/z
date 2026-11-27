//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmPlatform.hh>

#include "ZpktAfpPool.hh"

namespace Zpkt::Afp {

static ZeException poolError(const char *operation, int err)
{
  return ZeEXCEPT(Error, "Zpkt.Afp", ([operation, err](auto &s) {
    s << operation << ": " << ZeError{err};
  }));
}

static bool alignUp(size_t n, size_t alignment, size_t &out)
{
  if (__builtin_add_overflow(n, alignment - 1, &out)) return false;
  out &= ~(alignment - 1);
  return true;
}

BufPool::Prepared::Prepared(Prepared &&other) noexcept :
  m_geometry{other.m_geometry}, m_slab{other.m_slab}
{
  other.m_slab = nullptr;
}

BufPool::Prepared &BufPool::Prepared::operator =(Prepared &&other) noexcept
{
  ::free(m_slab);
  m_geometry = other.m_geometry;
  m_slab = other.m_slab;
  other.m_slab = nullptr;
  return *this;
}

BufPool::Prepared::~Prepared() { ::free(m_slab); }

int BufPool::Prepared::init(BufPool &pool)
{
  if (pool.allocated() || m_slab) return EBUSY;
  if (!pool.count || !pool.size || !pool.align || (pool.align & (pool.align - 1)) ||
      pool.headRoom > pool.size || pool.tailRoom > pool.size - pool.headRoom)
    return EINVAL;

  constexpr size_t line = Zm::CacheLineSize;
  ZuAssert(!(line & (line - 1)) && line >= alignof(Slot));
  Geometry geometry;
  geometry.alignment = pool.align > line ? pool.align : line;
  size_t slotSize;
  if (!alignUp(sizeof(Slot), geometry.alignment, geometry.prefix) ||
      !alignUp(pool.size, line, geometry.backing) ||
      __builtin_add_overflow(geometry.prefix, geometry.backing, &slotSize) ||
      !alignUp(slotSize, geometry.alignment, geometry.stride) ||
      __builtin_mul_overflow(geometry.stride, pool.count, &geometry.bytes) ||
      geometry.bytes > PTRDIFF_MAX)
    return EOVERFLOW;

  void *slab;
  if (int rc = posix_memalign(&slab, geometry.alignment, geometry.bytes)) return rc;
  m_slab = static_cast<uint8_t *>(slab);
  m_geometry = geometry;
  for (unsigned i = 0; i < pool.count; ++i) {
    uintptr_t next = i + 1 < pool.count ? uintptr_t(at(i + 1)) : 0;
    new (at(i)) Slot{&pool, next};
  }
  return 0;
}

BufPool::BufPool(
  Zi::IOBufType::T type_, unsigned count_, unsigned size_,
  unsigned headRoom_, unsigned tailRoom_, unsigned align_) :
  Zi::IOBufPool{type_}
{
  count = count_;
  size = size_;
  headRoom = headRoom_;
  tailRoom = tailRoom_;
  align = align_;
}

BufPool::~BufPool() { ZmAssert_(!used()); }

void BufPool::publish(Prepared &&prepared)
{
  ZmAssert_(!allocated() && prepared);
  m_storage = ZuMv(prepared);
  m_head = uintptr_t(m_storage.at(0));
}

void BufPool::final()
{
  ZmAssert_(!used());
  m_head = 0;
  m_returned = {};
  m_storage = Prepared{};
}

uint8_t *BufPool::alloc()
{
  uintptr_t p;
  for (;;) {
    p = m_head.load_();
    if (ZuUnlikely(!p)) return nullptr;
    if (ZuUnlikely(p & 1)) { ZmAtomic_acquire(); continue; }
    if (m_head.cmpXch(p | 1, p) == p) break;
  }
  auto slot = reinterpret_cast<Slot *>(p);
  ++m_used;
  slot->leases.store_(Slot::Caller);
  m_head = slot->next;
  return slot->packet();
}

void BufPool::free(uint8_t *packet)
{
  auto slot = find(packet);
  ZmAssert_(slot);
  slot->release(Slot::Caller);
}

void BufPool::discard(uint8_t *packet)
{
  auto slot = find(packet);
  ZmAssert_(slot);
  slot->release(Slot::Caller, false);
}

Slot *BufPool::find(const void *packet) const
{
  if (ZuUnlikely(!m_storage)) return nullptr;
  auto ptr = uintptr_t(packet), begin = m_storage.begin();
  if (ptr < begin || ptr >= m_storage.end()) return nullptr;
  auto offset = ptr - begin, stride = m_storage.geometry().stride;
  auto i = offset/stride;
  return offset - i * stride < size ? m_storage.at(unsigned(i)) : nullptr;
}

void BufPool::recycle(Slot *slot, bool notify)
{
  // Lock-marked head protects a pop across reading next. A slot is
  // exclusively ours until the successful push publishes it.
  for (;;) {
    auto next = m_head.load_();
    if (next & 1) { ZmAtomic_acquire(); continue; }
    slot->next = next;
    if (m_head.cmpXch(uintptr_t(slot), next) == next) break;
  }
  if (notify && m_returned) m_returned();
  ZmAssert_(m_used--);
}

uint8_t *Slot::packet()
{
  return reinterpret_cast<uint8_t *>(this) + pool->geometry().prefix;
}

void Slot::release(unsigned lease, bool notify)
{
  unsigned old = leases.xchAnd(~lease);
  ZmAssert_(old & lease);
  if (old == lease) pool->recycle(this, notify);
}

PoolSet::~PoolSet() { ZmAssert_(final()); }

PoolSet::Result PoolSet::alloc(
  Zi::IOBufPool *rxPool, const Zi::IOBufPoolList &txPools,
  const PoolRefs &reserves, unsigned mtu)
{
  if (allocated()) {
    if (rxPool == m_rx.ptr() && txPools.count_() == m_appTx &&
	reserves.length() == m_tx.length() - m_appTx) {
      bool same = true;
      {
	auto it = txPools.citer();
	while (auto pool = it()) {
	  bool found = false;
	  for (unsigned i = 0; i < m_appTx; ++i) found |= pool == m_tx[i].ptr();
	  same &= found;
	}
      }
      for (unsigned i = 0; i < reserves.length(); ++i)
	same &= reserves[i].ptr() == m_tx[m_appTx + i].ptr();
      if (same) return {};
    }
    return poolError("conflicting AFP pool set", EINVAL);
  }

  auto ingress = dynamic_cast<BufPool *>(rxPool);
  if (!ingress || ingress->type != Zi::IOBufType::Rx || ingress->size < 14 ||
      ingress->size - 14 < mtu)
    return poolError("invalid AFP ingress pool or frame geometry", EINVAL);

  PoolRefs tx;
  size_t capacity = 0;
  auto add = [&tx, &capacity](BufPool *pool) {
    if (!pool || !pool->count ||
	(pool->type != Zi::IOBufType::TxTCP && pool->type != Zi::IOBufType::TxUDP))
      return false;
    for (const auto &seen : tx) if (seen.ptr() == pool) return false;
    if (__builtin_add_overflow(capacity, pool->count, &capacity) || capacity > UINT_MAX)
      return false;
    tx.push(ZmRef<BufPool>{pool});
    return true;
  };
  {
    auto it = txPools.citer();
    while (auto pool = it())
      if (!add(dynamic_cast<BufPool *>(pool))) return poolError("invalid AFP Tx pool", EINVAL);
  }
  unsigned appTx = tx.length();
  for (const auto &pool : reserves)
    if (!add(pool.ptr())) return poolError("invalid or duplicate AFP Tx reserve", EINVAL);
  if (!capacity || capacity > PTRDIFF_MAX/sizeof(Completion))
    return poolError("invalid AFP Tx completion capacity", EINVAL);

  BufPool::Prepared rxBacking;
  if (int err = ingress->prepare(rxBacking)) return poolError("allocate AFP Rx pool", err);
  ZtArray<BufPool::Prepared, ZtArrayHeapID<"Zpkt.Afp.Prepared">> backing;
  backing.length(tx.length());
  Index index;
  index.length(tx.length());
  for (unsigned i = 0; i < tx.length(); ++i) {
    if (int err = tx[i]->prepare(backing[i])) return poolError("allocate AFP Tx pool", err);
    index[i] = {backing[i].begin(), backing[i].end(), tx[i].ptr()};
  }
  qsort(index.data(), index.length(), sizeof(Range), [](const void *a, const void *b) {
    auto lhs = static_cast<const Range *>(a)->begin;
    auto rhs = static_cast<const Range *>(b)->begin;
    return (lhs > rhs) - (lhs < rhs);
  });
  m_completions.length(capacity);
  m_appTx = appTx;
  m_rx = ingress;
  m_tx = ZuMv(tx);
  m_index = ZuMv(index);
  m_rx->publish(ZuMv(rxBacking));
  for (unsigned i = 0; i < m_tx.length(); ++i) m_tx[i]->publish(ZuMv(backing[i]));
  return {};
}

bool PoolSet::final()
{
  if (m_rx && m_rx->used()) return false;
  for (const auto &pool : m_tx) if (pool->used()) return false;
  if (m_rx) m_rx->final();
  for (const auto &pool : m_tx) pool->final();
  m_index.null();
  m_rx = nullptr;
  m_tx.null();
  m_appTx = 0;
  m_completions.null();
  return true;
}

Slot *PoolSet::findTx(const void *packet) const
{
  auto address = uintptr_t(packet);
  unsigned lo = 0, hi = m_index.length();
  while (lo < hi) {
    unsigned mid = lo + ((hi - lo)>>1);
    const auto &range = m_index[mid];
    if (address < range.begin) hi = mid;
    else if (address >= range.end) lo = mid + 1;
    else return range.pool->find(packet);
  }
  return nullptr;
}

} // namespace Zpkt::Afp
