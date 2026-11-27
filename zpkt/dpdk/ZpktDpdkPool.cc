//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <limits.h>

#include <new>

#include <rte_errno.h>
#include <rte_malloc.h>
#include <rte_mbuf_pool_ops.h>
#include <rte_memzone.h>
#include <rte_ring.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuHash.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmAtomic.hh>

#include "ZpktDpdkPool.hh"

namespace Zpkt::Dpdk {

static_assert(Zm::CacheLineSize >= 64);

static int makeRing(rte_ring *&ring, unsigned count, int socket, unsigned flags)
{
  if (!count || count > (RTE_RING_SZ_MASK>>1)) return -EINVAL;
  auto capacity = rte_align32pow2(count + 1);
  auto bytes = rte_ring_get_memsize(capacity);
  if (bytes < 0) return int(bytes);
  ring = static_cast<rte_ring *>(
    rte_zmalloc_socket("zpkt_ring", size_t(bytes), RTE_CACHE_LINE_SIZE, socket));
  if (!ring) return -ENOMEM;
  auto rc = rte_ring_init(ring, "zpkt_ring", capacity, flags);
  if (rc < 0) {
    rte_free(ring);
    ring = nullptr;
  }
  return rc;
}

struct TxPoolState {
  rte_ring	*returned = nullptr, *available = nullptr;
  TxReturns	*ready = nullptr;
  bool		online = false;
  alignas(Zm::CacheLineSize) ZmAtomic<unsigned> scheduled{0};
};

static auto *txState(const rte_mempool *pool)
{
  return static_cast<TxPoolState *>(pool->pool_data);
}

static int txPoolAlloc(rte_mempool *pool)
{
  auto mem = rte_zmalloc_socket(
    "zpkt_tx_state", sizeof(TxPoolState), alignof(TxPoolState), pool->socket_id);
  if (!mem) return -ENOMEM;
  auto state = new (mem) TxPoolState;
  state->ready = static_cast<TxReturns *>(pool->pool_config);
  auto rc = makeRing(state->returned, pool->size, pool->socket_id, RING_F_SC_DEQ);
  if (!rc) rc = makeRing(state->available, pool->size, pool->socket_id, 0);
  if (rc < 0) {
    rte_free(state->returned);
    rte_free(state);
    return rc;
  }
  pool->pool_data = state;
  return 0;
}

static void txPoolFree(rte_mempool *pool)
{
  if (auto state = txState(pool)) {
    rte_free(state->available);
    rte_free(state->returned);
    rte_free(state);
  }
}

static int txPoolEnqueue(rte_mempool *pool, void *const *objects, unsigned n)
{
  if (!n) return 0;
  auto state = txState(pool);
  auto ready = state->online ? state->ready : nullptr;
  if (ready) ready->enter();
  ZmAssert_(rte_ring_mp_enqueue_bulk(state->returned, objects, n, nullptr) == n);
  if (ready) {
#ifdef ZPKT_DPDK_TEST
    ZpktTestDpdkReturns(ReturnPoint::Published, n);
#endif
    // The scheduling exchange acquires; publish the return before it.
    ZmAtomic_release();
    if (!state->scheduled.xch(1)) ready->push(pool);
    ready->leave();
  }
  return 0;
}

static int txPoolDequeue(rte_mempool *pool, void **objects, unsigned n)
{
  return rte_ring_sc_dequeue_bulk(txState(pool)->returned, objects, n, nullptr) == n ?
    0 : -ENOENT;
}

static unsigned txPoolCount(const rte_mempool *pool)
{
  return rte_ring_count(txState(pool)->returned);
}

static int setTxOps(rte_mempool *pool, TxReturns *ready)
{
  static const rte_mempool_ops ops{
    .name = "zpkt_tx_returns",
    .alloc = txPoolAlloc,
    .free = txPoolFree,
    .enqueue = txPoolEnqueue,
    .dequeue = txPoolDequeue,
    .get_count = txPoolCount
  };
  static const int registered = rte_mempool_register_ops(&ops);
  if (registered < 0) return registered;
  return rte_mempool_set_ops_byname(pool, ops.name, ready);
}

TxReturns::~TxReturns() { reset(); }

int TxReturns::init(unsigned count, int socket)
{
  ZmAssert_(!m_ready);
  return makeRing(m_ready, count, socket, RING_F_SC_DEQ);
}

void TxReturns::reset()
{
  if (m_ready) {
    ZmAssert_(!active() && rte_ring_empty(m_ready));
    rte_free(m_ready);
    m_ready = nullptr;
  }
}

void TxReturns::push(rte_mempool *pool)
{
#ifdef ZPKT_DPDK_TEST
  ZpktTestDpdkReturns(ReturnPoint::QueuePush, 1);
#endif
  ZmAssert_(!rte_ring_mp_enqueue(m_ready, pool));
}

rte_mempool *TxReturns::shift()
{
  void *object;
  if (!m_ready || rte_ring_sc_dequeue(m_ready, &object)) return nullptr;
#ifdef ZPKT_DPDK_TEST
  ZpktTestDpdkReturns(ReturnPoint::QueuePop, 1);
#endif
  return static_cast<rte_mempool *>(object);
}

void TxReturns::settle(rte_mempool *pool)
{
  auto state = txState(pool);
  if (!rte_ring_empty(state->returned)) {
    push(pool);
    return;
  }
  // A return that publishes before the clear is visible to this recheck.
  // Otherwise the returning thread or this consumer claims the pool anew.
#ifdef ZPKT_DPDK_TEST
  ZpktTestDpdkReturns(ReturnPoint::BeforeClear, 0);
#endif
  ZmAtomic_release();
  state->scheduled.xch(0);
#ifdef ZPKT_DPDK_TEST
  ZpktTestDpdkReturns(ReturnPoint::AfterClear, 0);
#endif
  if (!rte_ring_empty(state->returned) && !state->scheduled.xch(1))
    push(pool);
#ifdef ZPKT_DPDK_TEST
  ZpktTestDpdkReturns(ReturnPoint::AfterRecheck, 0);
#endif
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

BufPool::Prepared::~Prepared() { reset(); }

void BufPool::Prepared::reset()
{
  if (!m_pool) return;
  if (m_tx)
    if (auto state = txState(m_pool)) {
      // Also restores the seeded prefix if allocation fails before publication.
      state->online = false;
      void *objects[32];
      while (auto count = rte_ring_dequeue_burst(state->available, objects, 32, nullptr))
	rte_mempool_put_bulk(m_pool, objects, count);
      ZmAssert_(rte_mempool_avail_count(m_pool) == m_pool->populated_size);
    }
  rte_mempool_free(m_pool);
  m_pool = nullptr;
  m_tx = false;
}

BufPool::Prepared &BufPool::Prepared::operator =(Prepared &&other) noexcept
{
  if (this == &other) return *this;
  reset();
  m_pool = other.m_pool;
  m_tx = other.m_tx;
  other.m_pool = nullptr;
  other.m_tx = false;
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
  unsigned align, int socket, TxReturns *returns)
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
  auto rc = returns ? setTxOps(pool, returns) :
    rte_mempool_set_ops_byname(pool, rte_mbuf_best_mempool_ops(), nullptr);
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

static int nativeTxPool(
  rte_mempool *&pool, const char *name, unsigned count, unsigned room, unsigned priv,
  int socket, TxReturns *returns)
{
  pool = rte_mempool_create_empty(name, count, sizeof(rte_mbuf) + priv + room, 0,
    sizeof(rte_pktmbuf_pool_private), socket, 0);
  if (!pool) return rte_errno ? -rte_errno : -ENOMEM;
  auto rc = setTxOps(pool, returns);
  if (rc < 0) return rc;
  rte_pktmbuf_pool_private layout{};
  layout.mbuf_data_room_size = uint16_t(room);
  layout.mbuf_priv_size = uint16_t(priv);
  rte_pktmbuf_pool_init(pool, &layout);
  rc = rte_mempool_populate_default(pool);
  if (rc < 0) return rc;
  if (unsigned(rc) != count) return -ENOMEM;
  rte_mempool_obj_iter(pool, rte_pktmbuf_init, nullptr);
  return 0;
}

int BufPool::prepare(Prepared &dest, int socket, unsigned minDataSize, TxReturns *returns)
{
  if (native() || dest.native()) return -EBUSY;
  if (type != Zi::IOBufType::Rx && type != Zi::IOBufType::TxTCP && type != Zi::IOBufType::TxUDP)
    return -EINVAL;
  if ((type != Zi::IOBufType::Rx) != bool(returns)) return -EINVAL;
  constexpr unsigned mask = Zm::CacheLineSize - 1;
  auto extent = RTE_MAX(size, minDataSize);
  if (!count || count > INT_MAX || !size || extent > UINT_MAX - mask ||
      headRoom > size || tailRoom > size - headRoom ||
      !align || align > (1U<<16) || (align & (align - 1))) return -EINVAL;
  auto dataSize = (extent + mask) & ~mask;
  if (dataSize > UINT16_MAX - RTE_PKTMBUF_HEADROOM) return -EINVAL;

  Prepared backing;
  if (int rc = backing.init(*this, socket, dataSize, returns)) return rc;
  dest = ZuMv(backing);
  return 0;
}

int BufPool::Prepared::init(BufPool &pool, int socket, unsigned dataSize, TxReturns *returns)
{
  ZmAssert_(!m_pool);
  static ZmAtomic<unsigned> ids{0};
  ZuCArray<RTE_MEMPOOL_NAMESIZE> name;
  name << "zpkt_" << ZuBoxed(ids++);
  auto room = dataSize + unsigned(RTE_PKTMBUF_HEADROOM);
  auto alignment = RTE_MAX(pool.align, unsigned(Zm::CacheLineSize));
  m_tx = bool(returns);
  if (alignment <= RTE_MEMPOOL_ALIGN) {
    constexpr auto prefix = sizeof(rte_mbuf) + RTE_PKTMBUF_HEADROOM + sizeof(Slot);
    constexpr unsigned priv = sizeof(Slot) + ((-prefix) & RTE_MEMPOOL_ALIGN_MASK);
    static_assert(!(priv % RTE_MBUF_PRIV_ALIGN));
    if (returns) {
      auto rc = nativeTxPool(m_pool, name.terminate(), pool.count, room, priv, socket, returns);
      if (rc < 0) return rc;
    } else {
      m_pool = rte_pktmbuf_pool_create(
	name.terminate(), pool.count, 0, priv, uint16_t(room), socket);
      if (!m_pool) return rte_errno ? -rte_errno : -ENOMEM;
    }
  } else {
    auto rc = alignedPool(m_pool, name.terminate(), pool.count, room, alignment, socket, returns);
    if (rc < 0) return rc;
  }
  struct Init {
    BufPool *pool;
    unsigned alignment;
    bool aligned = true;
  } init{&pool, alignment};
  rte_mempool_obj_iter(m_pool, [](rte_mempool *, void *opaque, void *obj, unsigned) {
    auto &init = *static_cast<Init *>(opaque);
    auto m = static_cast<rte_mbuf *>(obj);
    auto p = rte_pktmbuf_mtod(m, uint8_t *);
    init.aligned &= !(uintptr_t(p) & (init.alignment - 1));
    new (slot(m)) Slot{.pool = init.pool, .mbuf = m, .packet = p};
  }, &init);
  if (!init.aligned) return -EINVAL;
  if (int rc = m_index.init({&m_pool, 1})) return rc;
  if (returns) {
    auto available = txState(m_pool)->available;
    for (unsigned left = pool.count; left;) {
      void *objects[32];
      unsigned count = RTE_MIN(left, 32U);
      if (int rc = rte_mempool_get_bulk(m_pool, objects, count)) return rc;
      ZmAssert_(rte_ring_enqueue_bulk(available, objects, count, nullptr) == count);
      left -= count;
    }
  }
  return 0;
}

void BufPool::publish(Prepared &&backing)
{
  ZmAssert_(!native() && backing.native());
  m_backing = ZuMv(backing);
  if (type != Zi::IOBufType::Rx) txState(native())->online = true;
}

unsigned BufPool::available() const
{
  auto pool = native();
  if (!pool) return 0;
  return type == Zi::IOBufType::Rx ? rte_mempool_avail_count(pool) :
    rte_ring_count(txState(pool)->available);
}

bool BufPool::final()
{
  if (native() && available() != count) return false;
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
  rte_mbuf *m;
  if (type == Zi::IOBufType::Rx) {
    m = rte_pktmbuf_alloc(pool);
    if (ZuUnlikely(!m)) return nullptr;
  } else {
    void *object;
    if (ZuUnlikely(rte_ring_dequeue(txState(pool)->available, &object))) return nullptr;
    m = static_cast<rte_mbuf *>(object);
    resetTx(m);
  }
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
  if (type == Zi::IOBufType::Rx) {
    rte_pktmbuf_free(m);
    if (m_returned) m_returned();
  } else {
    ZmAssert_(!rte_ring_enqueue(txState(m->pool)->available, m));
  }
}

} // namespace Zpkt::Dpdk
