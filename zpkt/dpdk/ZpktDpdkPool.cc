//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <limits.h>

#include <new>

#include <rte_errno.h>
#include <rte_mbuf_pool_ops.h>
#include <rte_memzone.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuHash.hh>

#include <zlib/ZmAssert.hh>

#include "ZpktDpdkPool.hh"

namespace Zpkt::Dpdk {

static_assert(RTE_MEMPOOL_ALIGN >= 64 && !(RTE_PKTMBUF_HEADROOM % RTE_MEMPOOL_ALIGN));
#ifndef RTE_MBUF_REFCNT_ATOMIC
#error DPDK mbuf reference counting must be atomic for Zpkt
#endif

bool Slot::submit()
{
  if (leases.cmpXch(Caller | Submitted, Caller) != Caller) return false;
  rte_mbuf_refcnt_update(mbuf, 1);
  return true;
}

bool Slot::retired() const
{
  if (rte_mbuf_refcnt_read(mbuf) != 1) return false;
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  return true;
}

void Slot::releaseSubmission()
{
  ZmAssert_(retired());
  release(Submitted);
}

void Slot::release(unsigned lease)
{
  auto prev = leases.xchAnd(~lease);
  ZmAssert_(prev & lease);
  if (prev == lease) pool->recycle(mbuf);
}

enum { PageShift = 12, LineShift = 6, PageLines = 64 };

static uintptr_t pageKey(uintptr_t address) { return (address>>PageShift) + 1; }
static unsigned line(uintptr_t address) { return (address>>LineShift) & (PageLines - 1); }

AddressIndex::AddressIndex(AddressIndex &&other) noexcept
{
  *this = ZuMv(other);
}

AddressIndex &AddressIndex::operator =(AddressIndex &&other) noexcept
{
  if (this == &other) return *this;
  m_pages = ZuMv(other.m_pages);
  m_slots = ZuMv(other.m_slots);
  m_shift = other.m_shift;
  other.reset();
  return *this;
}

void AddressIndex::reset()
{
  m_pages.null();
  m_slots.null();
  m_shift = 0;
}

unsigned AddressIndex::page(uintptr_t key) const
{
  // Use high hash bits to spread sequential keys across contiguous probes.
  auto pos = unsigned(ZuHash<uintptr_t>::hash(key)>>m_shift);
  auto mask = unsigned(m_pages.length() - 1);
  for (;;) {
    auto found = m_pages[pos].key;
    if (!found || found == key) return pos;
    pos = (pos + 1) & mask;
  }
}

int AddressIndex::init(ZuSpan<rte_mempool *const> pools)
{
  ZmAssert_(!m_pages.length());
  uint64_t pages = 0, slots = 0;
  for (auto pool : pools) {
    if (!pool) return -EINVAL;
    slots += pool->size;
    rte_mempool_obj_iter(pool, [](rte_mempool *, void *opaque, void *obj, unsigned) {
      auto slot = BufPool::slot(static_cast<rte_mbuf *>(obj));
      auto begin = uintptr_t(slot->packet);
      *static_cast<uint64_t *>(opaque) += pageKey(begin + slot->pool->size - 1) -
	pageKey(begin) + 1;
    }, &pages);
  }
  if (!slots) return -EINVAL;
  if (pages > UINT_MAX/4 || slots > UINT_MAX) return -EOVERFLOW;
  auto capacity = rte_align32pow2(unsigned(pages)<<1);
  m_pages.length(capacity);
  m_slots.length(slots);
  m_shift = 32 - __builtin_ctz(capacity);
  for (auto pool : pools)
    rte_mempool_obj_iter(pool, [](rte_mempool *, void *opaque, void *obj, unsigned) {
      auto self = static_cast<AddressIndex *>(opaque);
      auto slot = BufPool::slot(static_cast<rte_mbuf *>(obj));
      auto address = uintptr_t(slot->packet);
      auto begin = pageKey(address);
      auto end = pageKey(address + slot->pool->size - 1);
      auto &first = self->m_pages[self->page(begin)];
      auto bit = uint64_t(1)<<line(address);
      ZmAssert_(!(first.starts & bit));
      first.key = begin;
      first.starts |= bit;
      for (auto key = begin + 1; key <= end; ++key) {
	auto &part = self->m_pages[self->page(key)];
	ZmAssert_(!part.carry);
	part.key = key;
	part.carry = slot;
      }
    }, this);
  unsigned next = 0;
  for (auto &p : m_pages) {
    p.first = next;
    next += __builtin_popcountll(p.starts);
  }
  for (auto pool : pools)
    rte_mempool_obj_iter(pool, [](rte_mempool *, void *opaque, void *obj, unsigned) {
      auto self = static_cast<AddressIndex *>(opaque);
      auto slot = BufPool::slot(static_cast<rte_mbuf *>(obj));
      auto address = uintptr_t(slot->packet);
      const auto &p = self->m_pages[self->page(pageKey(address))];
      auto lower = (uint64_t(1)<<line(address)) - 1;
      self->m_slots[p.first + __builtin_popcountll(p.starts & lower)] = slot;
    }, this);
  return 0;
}

Slot *AddressIndex::find(const void *packet) const
{
  if (!m_pages.length()) return nullptr;
  auto address = uintptr_t(packet);
  const auto &p = m_pages[page(pageKey(address))];
  if (!p.key) return nullptr;
  auto starts = p.starts & (~uint64_t(0)>>(PageLines - 1 - line(address)));
  auto slot = starts ? m_slots[p.first + __builtin_popcountll(starts) - 1] : p.carry;
  if (!slot || address - uintptr_t(slot->packet) >= slot->pool->size) return nullptr;
  return slot;
}

BufPool::Prepared::Prepared(Prepared &&other) noexcept
{
  *this = ZuMv(other);
}

BufPool::Prepared::~Prepared()
{
  if (m_pool) rte_mempool_free(m_pool);
}

BufPool::Prepared &BufPool::Prepared::operator =(Prepared &&other) noexcept
{
  if (this == &other) return *this;
  if (m_pool) rte_mempool_free(m_pool);
  m_pool = other.m_pool;
  other.m_pool = nullptr;
  m_index = ZuMv(other.m_index);
  return *this;
}

BufPool::BufPool(
  Zi::IOBufType::T type, unsigned n, unsigned bytes, unsigned head, unsigned tail, unsigned a)
  : Zi::IOBufPool{type}
{
  count = n;
  size = bytes;
  headRoom = head;
  tailRoom = tail;
  align = a;
}

BufPool::~BufPool() { ZmAssert_(final()); }

static int alignedPool(
  rte_mempool *&pool, const char *name, unsigned count, unsigned room,
  unsigned align, int socket)
{
  // Use direct mbufs with standard native headroom. Padding the private area
  // aligns every packet's first byte, and padding the object stride keeps
  // that alignment consistent without changing the advertised data room.
  constexpr unsigned flags = RTE_MEMPOOL_F_NO_SPREAD;
  rte_mempool_objsz geometry;
  rte_mempool_calc_obj_size(sizeof(rte_mbuf), flags, &geometry);
  auto prefix = geometry.header_size + sizeof(rte_mbuf) +
    unsigned(RTE_PKTMBUF_HEADROOM) + sizeof(Slot);
  auto priv = sizeof(Slot) + ((-prefix) & (align - 1));
  if (priv > UINT16_MAX || priv % RTE_MBUF_PRIV_ALIGN) return -EINVAL;
  auto elt = unsigned(sizeof(rte_mbuf) + priv + room);
  auto bytes = rte_mempool_calc_obj_size(elt, flags, &geometry);
  auto stride = rte_align32pow2(bytes);
  pool = rte_mempool_create_empty(name, count, elt + stride - bytes, 0,
    sizeof(rte_pktmbuf_pool_private), socket, flags);
  if (!pool) return rte_errno ? -rte_errno : -ENOMEM;
  auto rc = rte_mempool_set_ops_byname(pool, rte_mbuf_best_mempool_ops(), nullptr);
  if (rc < 0) return rc;
  rte_pktmbuf_pool_private layout{};
  layout.mbuf_data_room_size = uint16_t(room);
  layout.mbuf_priv_size = uint16_t(priv);
  rte_pktmbuf_pool_init(pool, &layout);
  auto zone = rte_memzone_reserve_aligned(
    name, size_t(count) * stride, socket, RTE_MEMZONE_IOVA_CONTIG, stride);
  if (!zone) return rte_errno ? -rte_errno : -ENOMEM;
  rc = rte_mempool_populate_iova(pool, static_cast<char *>(zone->addr), zone->iova, zone->len,
    [](rte_mempool_memhdr *, void *opaque) {
      rte_memzone_free(static_cast<const rte_memzone *>(opaque));
    }, const_cast<rte_memzone *>(zone));
  if (rc <= 0) rte_memzone_free(zone);
  if (rc < 0) return rc;
  if (unsigned(rc) != count) return -ENOMEM;
  rte_mempool_obj_iter(pool, rte_pktmbuf_init, nullptr);
  return 0;
}

int BufPool::prepare(Prepared &dest, int socket, unsigned minDataSize)
{
  if (native() || dest.native()) return -EBUSY;
  if (type != Zi::IOBufType::Rx && type != Zi::IOBufType::TxTCP && type != Zi::IOBufType::TxUDP)
    return -EINVAL;
  auto dataSize = RTE_MAX(size, minDataSize);
  if (!count || count > INT_MAX || !size || dataSize > UINT16_MAX - RTE_PKTMBUF_HEADROOM ||
      headRoom > size || tailRoom > size - headRoom ||
      !align || align > (1U<<16) || (align & (align - 1))) return -EINVAL;

  Prepared backing;
  if (int rc = backing.init(*this, socket, dataSize)) return rc;
  dest = ZuMv(backing);
  return 0;
}

int BufPool::Prepared::init(BufPool &pool, int socket, unsigned dataSize)
{
  ZmAssert_(!m_pool);
  static ZmAtomic<unsigned> ids{0};
  ZuCArray<RTE_MEMPOOL_NAMESIZE> name;
  name << "zpkt_" << ZuBoxed(ids++);
  auto room = dataSize + unsigned(RTE_PKTMBUF_HEADROOM);
  if (pool.align <= RTE_MEMPOOL_ALIGN) {
    constexpr unsigned priv = (sizeof(Slot) + RTE_MEMPOOL_ALIGN - 1) & ~RTE_MEMPOOL_ALIGN_MASK;
    m_pool = rte_pktmbuf_pool_create(name.terminate(), pool.count, 0, priv, uint16_t(room), socket);
    if (!m_pool) return rte_errno ? -rte_errno : -ENOMEM;
  } else {
    auto rc = alignedPool(m_pool, name.terminate(), pool.count, room, pool.align, socket);
    if (rc < 0) return rc;
  }
  struct Init {
    BufPool *pool;
    bool aligned = true;
  } init{&pool};
  rte_mempool_obj_iter(m_pool, [](rte_mempool *, void *opaque, void *obj, unsigned) {
    auto &init = *static_cast<Init *>(opaque);
    auto m = static_cast<rte_mbuf *>(obj);
    auto p = rte_pktmbuf_mtod(m, uint8_t *);
    auto align = RTE_MAX(init.pool->align, 1U<<LineShift);
    init.aligned &= !(uintptr_t(p) & (align - 1));
    new (slot(m)) Slot{.pool = init.pool, .mbuf = m, .packet = p};
  }, &init);
  if (!init.aligned) return -EINVAL;
  return m_index.init({&m_pool, 1});
}

void BufPool::publish(Prepared &&backing)
{
  ZmAssert_(!native() && backing.native());
  m_backing = ZuMv(backing);
}

bool BufPool::final()
{
  if (native() && rte_mempool_in_use_count(native())) return false;
  m_returned = {};
  m_backing = {};
  return true;
}

bool BufPool::adopt(rte_mbuf *m)
{
  if (!native() || m->pool != native()) return false;
  auto s = slot(m);
  if (rte_pktmbuf_mtod(m, uint8_t *) != s->packet) return false;
  return s->leases.cmpXch(Slot::Caller, 0) == 0;
}

uint8_t *BufPool::alloc()
{
  auto pool = native();
  if (ZuUnlikely(!pool)) return nullptr;
  auto m = rte_pktmbuf_alloc(pool);
  if (ZuUnlikely(!m)) return nullptr;
  auto s = slot(m);
  ZmAssert_(!unsigned(s->leases));
  s->leases = Slot::Caller;
  return s->packet;
}

void BufPool::free(uint8_t *packet)
{
  auto s = m_backing.find(packet);
  ZmAssert_(s);
  s->release(Slot::Caller);
}

void BufPool::recycle(rte_mbuf *m)
{
  rte_pktmbuf_free(m);
  if (m_returned) m_returned();
}

} // namespace Zpkt::Dpdk
