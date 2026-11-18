//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/bpf.h>

#include <zlib/ZmAssert.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZxdpUMem.hh>

namespace Zxdp::Xdp {

static unsigned power2(unsigned n)
{
  if (n > (unsigned(1)<<31)) return 0;
  if (n < 2) return 2;
  return unsigned(1)<<(32 - __builtin_clz(n - 1));
}

static unsigned poolStride(const Zi::IOBufPool &pool)
{
  unsigned size = pool.size;
  if (size < pool.align) size = pool.align;
  return power2(size < 64 ? 64 : size);
}

ZuUnion<ZmRef<UMem>, ZeException> UMem::create(
  ZuSpan<const ZmRef<Zi::IOBufPool>> txPools, Zi::IOBufPool *rxPool, unsigned mtu)
{
  auto fail = [](const char *text) { return ZeEXCEPT(Error, "Zxdp", text); };
  // Linux copy-mode Rx adds NET_IP_ALIGN before the Ethernet header.
  enum { RxAlign = 2 };
  auto rx = dynamic_cast<BufPool *>(rxPool);
  auto page = sysconf(_SC_PAGESIZE);
  if (page < 2048 || uint64_t(page) > UINT32_MAX || (page & (page - 1)))
    return fail("unsupported AF_XDP page size");
  if (!rx || !rx->count || rx->ready() || rx->used())
    return fail("one unused, nonempty Rx pool is required");
  if (!rx->align || (rx->align & (rx->align - 1)) || rx->align > unsigned(page) ||
      rx->headRoom > rx->size || rx->tailRoom > rx->size - rx->headRoom)
    return fail("invalid Rx pool geometry");
  unsigned prefix = rx->align > XDP_PACKET_HEADROOM ? rx->align : XDP_PACKET_HEADROOM;
  if (!mtu || rx->size < mtu + 14 || uint64_t(rx->size) + prefix + RxAlign > unsigned(page))
    return fail("Rx packet size does not fit the interface and one AF_XDP chunk");
  ArenaLayout layout;
  layout.prefix = prefix;
  layout.chunk = power2(rx->size + layout.prefix + RxAlign);
  if (layout.chunk < 2048) layout.chunk = 2048;
  layout.rxCount = rx->count;
  uint64_t txBytes = 0;
  {
    for (const auto &node : txPools) {
      auto pool = dynamic_cast<BufPool *>(node.ptr());
      if (!pool || pool == rx || pool->ready() || pool->used() || !pool->size ||
	pool->size > unsigned(page) || pool->headRoom > pool->size ||
	pool->tailRoom > pool->size - pool->headRoom || !pool->align ||
	(pool->align & (pool->align - 1)) || pool->align > unsigned(page))
	return fail("invalid or busy Tx pool");
      if (pool->count > UINT32_MAX - layout.txCount) return fail("Tx count overflow");
      layout.txCount += pool->count;
      auto stride = poolStride(*pool);
      if (stride > layout.chunk) layout.chunk = stride;
      auto bytes = uint64_t(pool->count) * stride;
      auto length = (bytes + page - 1) & ~(uint64_t(page) - 1);
      if (length > SIZE_MAX - txBytes) return fail("Tx arena size overflow");
      txBytes += length;
    }
  }
  if (!layout.txCount) return fail("at least one nonempty Tx pool is required");
  if (layout.chunk > unsigned(page)) return fail("packet exceeds AF_XDP chunk size");
  auto rxBytes = uint64_t(rx->count) * layout.chunk;
  rxBytes = (rxBytes + page - 1) & ~(uint64_t(page) - 1);
  if (rxBytes > SIZE_MAX - txBytes) return fail("Rx arena size overflow");
  layout.rxLength = rxBytes;
  layout.txLength = txBytes;
  layout.length = rxBytes + txBytes;
  if (layout.length/layout.chunk > UINT32_MAX || layout.length/unsigned(page) > UINT32_MAX)
    return fail("AF_XDP arena exceeds kernel count limits");
  ZmRef<UMem> arena = new UMem;
  auto mem = mmap(nullptr, layout.length, PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) {
    return ZeEXCEPT(Error, "Zxdp", ([error = ZeLastError](auto &out) {
      out << "AF_XDP arena mmap failed: " << error;
    }));
  }
  arena->m_data = static_cast<uint8_t *>(mem);
  arena->m_layout = layout;
  arena->m_pageShift = __builtin_ctz(unsigned(page));
  arena->m_pages.length(layout.length>>arena->m_pageShift);
  arena->m_tx.length(layout.txCount);
  rx->init(arena, 0, layout.chunk, layout.prefix, 0);
  for (size_t i = 0; i < layout.rxLength>>arena->m_pageShift; ++i) arena->m_pages[i] = rx;
  uint64_t offset = rxBytes;
  unsigned first = 0;
  for (const auto &node : txPools) {
    auto pool = static_cast<BufPool *>(node.ptr());
    auto stride = poolStride(*pool);
    auto bytes = (uint64_t(pool->count) * stride + page - 1) & ~(uint64_t(page) - 1);
    pool->init(arena, offset, stride, 0, first);
    for (auto i = offset>>arena->m_pageShift; i < (offset + bytes)>>arena->m_pageShift; ++i)
      arena->m_pages[i] = pool;
    first += pool->count;
    offset += bytes;
  }
  return arena;
}

BufPool *UMem::pool(const void *packet) const
{
  auto address = uintptr_t(packet), begin = uintptr_t(m_data);
  if (address < begin || address - begin >= m_layout.length) return nullptr;
  return m_pages[(address - begin)>>m_pageShift];
}
TxSlot &UMem::txSlot(const BufPool &pool, unsigned slot)
{
  return m_tx[pool.first() + slot];
}
void UMem::retire(const Zi::IOBufPoolList &txPools, BufPool *rx)
{
  rx->retire();
  auto pools = txPools.citer();
  while (auto node = pools()) static_cast<BufPool *>(node)->retire();
}
UMem::~UMem() { if (m_data) munmap(m_data, m_layout.length); }

static ZeException socketFailed(const char *operation)
{
  return ZeEXCEPT(Error, "Zxdp", ([operation, error = ZeLastError](auto &out) {
    out << operation << ": " << error;
  }));
}

template <typename R> ZuUnion<void, ZeException> Xsk::map(
  unsigned i, R &ring, const xdp_ring_offset &off, unsigned count, uint64_t offset)
{
  size_t length;
  if (!R::mapLength(off, count, length))
    return ZeEXCEPT(Error, "Zxdp", "invalid AF_XDP kernel ring offsets or mapping length");
  auto mem = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, offset);
  if (mem == MAP_FAILED) return socketFailed("AF_XDP ring mmap");
  m_maps[i] = {mem, length};
  if (!ring.init(mem, length, off, count))
    return ZeEXCEPT(Error, "Zxdp", "invalid AF_XDP mapped ring state");
  return {};
}

Xsk::~Xsk() { close(); }

ZuUnion<void, ZeException> Xsk::open(
  ZmRef<UMem> arena, unsigned ifindex, unsigned queue, unsigned rxCount, unsigned txCount,
  int shared)
{
  auto prepared = prepare(ZuMv(arena), rxCount, txCount, shared);
  if (prepared.is<ZeException>()) return ZuMv(prepared).p<ZeException>();
  auto bound = bind(ifindex, queue);
  if (bound.is<ZeException>()) { close(); return ZuMv(bound).p<ZeException>(); }
  return {};
}

ZuUnion<void, ZeException> Xsk::prepare(
  ZmRef<UMem> arena, unsigned rxCount, unsigned txCount, int shared)
{
  if (m_fd >= 0 || !arena || !rxCount || !txCount ||
      (rxCount & (rxCount - 1)) || (txCount & (txCount - 1)))
    return ZeEXCEPT(Error, "Zxdp", "invalid or already open AF_XDP registration/binding");
  m_fd = socket(AF_XDP, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (m_fd < 0) return socketFailed("AF_XDP socket");
  m_arena = ZuMv(arena);
  m_shared = shared;
  auto fail = [this](const char *operation) {
    auto error = socketFailed(operation);
    close();
    return error;
  };
  socklen_t namespaceLength = sizeof(m_namespace);
  if (getsockopt(m_fd, SOL_SOCKET, SO_NETNS_COOKIE, &m_namespace, &namespaceLength))
    return fail("AF_XDP socket namespace query");
  if (namespaceLength != sizeof(m_namespace)) {
    close();
    return ZeEXCEPT(Error, "Zxdp", "AF_XDP kernel lacks socket namespace identity");
  }
  const auto &layout = m_arena->layout();
  xdp_umem_reg reg{};
  reg.addr = uintptr_t(m_arena->data());
  reg.len = layout.length;
  reg.chunk_size = layout.chunk;
  reg.headroom = layout.prefix - XDP_PACKET_HEADROOM;
  if (shared < 0 && setsockopt(m_fd, SOL_XDP, XDP_UMEM_REG, &reg, sizeof(reg)))
    return fail("AF_XDP UMEM registration");
  // Linux limits the copy-mode drain budget to Tx ring capacity. 32K permits
  // sustained concurrent publication; CQ and pool credits still bound backing.
  enum { MinTxRing = 1<<15 };
  unsigned txRingCount = txCount < MinTxRing ? MinTxRing : txCount;
  struct { unsigned option, count; } rings[] = {
    {XDP_UMEM_FILL_RING, rxCount}, {XDP_UMEM_COMPLETION_RING, txCount},
    {XDP_RX_RING, rxCount}, {XDP_TX_RING, txRingCount}
  };
  for (auto ring : rings)
    if (setsockopt(m_fd, SOL_XDP, ring.option, &ring.count, sizeof(ring.count)))
      return fail("AF_XDP ring configuration");
  // Older kernels retain their default budget when this tuning option is absent.
  if (setsockopt(m_fd, SOL_XDP, XDP_MAX_TX_SKB_BUDGET,
	&txRingCount, sizeof(txRingCount)) && errno != ENOPROTOOPT)
    return fail("AF_XDP Tx budget configuration");
  xdp_mmap_offsets offsets{};
  socklen_t length = sizeof(offsets);
  if (getsockopt(m_fd, SOL_XDP, XDP_MMAP_OFFSETS, &offsets, &length))
    return fail("AF_XDP ring offset query");
  if (length < sizeof(offsets)) {
    close();
    return ZeEXCEPT(Error, "Zxdp", "AF_XDP kernel lacks required ring offset/flags ABI");
  }
  auto mapped = map(0, m_fill, offsets.fr, rxCount, XDP_UMEM_PGOFF_FILL_RING);
  if (mapped.is<void>()) mapped = map(1, m_comp, offsets.cr, txCount,
    XDP_UMEM_PGOFF_COMPLETION_RING);
  if (mapped.is<void>()) mapped = map(2, m_rx, offsets.rx, rxCount, XDP_PGOFF_RX_RING);
  if (mapped.is<void>()) mapped = map(3, m_tx, offsets.tx, txRingCount, XDP_PGOFF_TX_RING);
  if (mapped.is<ZeException>()) { close(); return ZuMv(mapped).p<ZeException>(); }
  return {};
}

ZuUnion<void, ZeException> Xsk::bind(unsigned ifindex, unsigned queue)
{
  return bind_(ifindex, queue, true);
}

ZuUnion<void, ZeException> Xsk::bind_(unsigned ifindex, unsigned queue, bool queryOptions)
{
  m_retryBind = false;
  if (m_fd < 0 || m_bound || !ifindex || ifindex > INT_MAX)
    return ZeEXCEPT(Error, "Zxdp", "invalid or already bound AF_XDP socket");
  sockaddr_xdp address{};
  address.sxdp_family = AF_XDP;
  address.sxdp_ifindex = ifindex;
  address.sxdp_queue_id = queue;
  // Ordinary binding lets the kernel select supported zero-copy operation.
  address.sxdp_flags = XDP_USE_NEED_WAKEUP;
  if (m_shared >= 0) {
    address.sxdp_flags = XDP_SHARED_UMEM; // inherits NEED_WAKEUP from the first socket
    address.sxdp_shared_umem_fd = m_shared;
  }
  if (::bind(m_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address))) {
    m_retryBind = errno == EBUSY;
    return socketFailed("AF_XDP interface/queue bind");
  }
  m_bound = true;
  m_ifindex = ifindex;
  m_queue = queue;
  if (!queryOptions) return {};
  xdp_options options{};
  socklen_t length = sizeof(options);
  if (getsockopt(m_fd, SOL_XDP, XDP_OPTIONS, &options, &length)) {
    auto error = socketFailed("AF_XDP capability query");
    close();
    return error;
  }
  if (length < sizeof(options)) {
    close();
    return ZeEXCEPT(Error, "Zxdp", "AF_XDP kernel lacks capability flags");
  }
  m_zeroCopy = options.flags & XDP_OPTIONS_ZEROCOPY;
  return {};
}

void Xsk::close()
{
  m_rx.freeze(); m_fill.freeze(); m_tx.freeze(); m_comp.freeze();
  if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
  m_rx.reset();
  m_fill.reset();
  m_tx.reset();
  m_comp.reset();
  for (unsigned i = 4; i; --i) {
    auto &mapping = m_maps[i - 1];
    if (mapping.addr) munmap(mapping.addr, mapping.length);
    mapping = {};
  }
  m_zeroCopy = false;
  m_bound = m_retryBind = false;
  m_ifindex = m_queue = 0;
  m_namespace = 0;
  m_shared = -1;
  m_arena = nullptr;
}

} // namespace Zxdp::Xdp
