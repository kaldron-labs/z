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

#include <zlib/ZpktUMem.hh>

namespace Zpkt::Xdp {

static unsigned poolPrefix(const Zi::IOBufPool &pool, bool metadata)
{
  if (!metadata) return 0;
  unsigned bytes = power2(sizeof(xsk_tx_metadata));
  return pool.align > bytes ? pool.align : bytes;
}

static unsigned poolStride(const Zi::IOBufPool &pool, bool metadata)
{
  unsigned size = pool.size + poolPrefix(pool, metadata);
  if (size < pool.align) size = pool.align;
  return power2(size < 64 ? 64 : size);
}

ZuUnion<ZmRef<UMem>, ZeException> UMem::create(
  ZuSpan<const ZmRef<Zi::IOBufPool>> txPools, Zi::IOBufPool *rxPool, unsigned mtu, bool metadata)
{
  auto fail = [](const char *text) { return ZeEXCEPT(Error, "Zpkt", text); };
  // Linux copy-mode Rx adds NET_IP_ALIGN before the Ethernet header.
  enum { RxAlign = 2 };
  auto rx = dynamic_cast<BufPool *>(rxPool);
  auto page = sysconf(_SC_PAGESIZE);
  if (page < 2048 || uint64_t(page) > UINT32_MAX || (page & (page - 1)))
    return fail("unsupported AF_XDP page size");
  if (!rx || rx->type != Zi::IOBufType::Rx || !rx->count || rx->ready() || rx->used())
    return fail("one unused, nonempty Rx pool is required");
  if (!rx->align || (rx->align & (rx->align - 1)) || rx->align > unsigned(page) ||
      rx->headRoom > rx->size || rx->tailRoom > rx->size - rx->headRoom)
    return fail("invalid Rx pool geometry");
  unsigned prefix = rx->align > XDP_PACKET_HEADROOM ? rx->align : XDP_PACKET_HEADROOM;
  if (!mtu || rx->size < mtu + 14 || uint64_t(rx->size) + prefix + RxAlign > unsigned(page))
    return fail("Rx packet size does not fit the interface and one AF_XDP chunk");
  // Keep logical capacities/alignment unchanged. A physical metadata prefix is
  // reserved only for an advertised offload and only when every pool still fits
  // a kernel chunk. Otherwise this arena uses caller software checksums.
  if (metadata) for (const auto &pool : txPools) {
    if (!pool || uint64_t(pool->size) + poolPrefix(*pool, true) > uint64_t(page)) {
      metadata = false;
      break;
    }
  }
  ArenaLayout layout;
  layout.txMetadata = metadata ? sizeof(xsk_tx_metadata) : 0;
  layout.prefix = prefix;
  layout.chunk = power2(rx->size + layout.prefix + RxAlign);
  if (layout.chunk < 2048) layout.chunk = 2048;
  layout.rxCount = rx->count;
  uint64_t txBytes = 0;
  {
    for (const auto &node : txPools) {
      auto pool = dynamic_cast<BufPool *>(node.ptr());
      if (!pool ||
	(pool->type != Zi::IOBufType::TxTCP && pool->type != Zi::IOBufType::TxUDP) ||
	pool->ready() || pool->used() || !pool->size ||
	pool->size > unsigned(page) || pool->headRoom > pool->size ||
	pool->tailRoom > pool->size - pool->headRoom || !pool->align ||
	(pool->align & (pool->align - 1)) || pool->align > unsigned(page))
	return fail("invalid or busy Tx pool");
      if (pool->count > UINT32_MAX - layout.txCount) return fail("Tx count overflow");
      layout.txCount += pool->count;
      auto stride = poolStride(*pool, metadata);
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
    return ZeEXCEPT(Error, "Zpkt", ([error = ZeLastError](auto &out) {
      out << "AF_XDP arena mmap failed: " << error;
    }));
  }
  arena->m_data = static_cast<uint8_t *>(mem);
  arena->m_layout = layout;
  arena->m_pageShift = __builtin_ctz(unsigned(page));
  arena->m_pages.length(layout.length>>arena->m_pageShift);
  arena->m_tx.length(layout.txCount);
  return arena;
}

void UMem::publish(ZuSpan<const ZmRef<Zi::IOBufPool>> txPools, BufPool *rx)
{
  unsigned page = 1U << m_pageShift;
  rx->init(this, 0, m_layout.chunk, m_layout.prefix, 0);
  for (size_t i = 0; i < m_layout.rxLength>>m_pageShift; ++i) m_pages[i] = rx;
  uint64_t offset = m_layout.rxLength;
  unsigned first = 0;
  for (const auto &node : txPools) {
    auto pool = static_cast<BufPool *>(node.ptr());
    bool metadata = m_layout.txMetadata;
    auto stride = poolStride(*pool, metadata);
    auto bytes = (uint64_t(pool->count) * stride + page - 1) & ~(uint64_t(page) - 1);
    pool->init(this, offset, stride, poolPrefix(*pool, metadata), first);
    for (auto i = offset>>m_pageShift; i < (offset + bytes)>>m_pageShift; ++i)
      m_pages[i] = pool;
    first += pool->count;
    offset += bytes;
  }
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
  return ZeEXCEPT(Error, "Zpkt", ([operation, error = ZeLastError](auto &out) {
    out << operation << ": " << error;
  }));
}

template <typename R> ZuUnion<void, ZeException> Xsk::map(
  unsigned i, R &ring, const xdp_ring_offset &off, unsigned count, uint64_t offset)
{
  size_t length;
  if (!R::mapLength(off, count, length))
    return ZeEXCEPT(Error, "Zpkt", "invalid AF_XDP kernel ring offsets or mapping length");
  auto mem = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, offset);
  if (mem == MAP_FAILED) return socketFailed("AF_XDP ring mmap");
  m_maps[i] = {mem, length};
  if (!ring.init(mem, length, off, count))
    return ZeEXCEPT(Error, "Zpkt", "invalid AF_XDP mapped ring state");
  return {};
}

Xsk::~Xsk() { close(); }

ZuUnion<void, ZeException> Xsk::owner(
  UMem *arena, unsigned ifindex, unsigned queue, unsigned fillCount, unsigned compCount, int shared)
{
  // A bound AF_XDP socket needs a packet ring even when it only owns FILL/CQ.
  return open(arena, ifindex, queue, fillCount, compCount, 2, 0, shared);
}

ZuUnion<void, ZeException> Xsk::service(
  unsigned ifindex, unsigned queue, unsigned rxCount, unsigned txCount, int shared)
{
  return open(nullptr, ifindex, queue, 0, 0, rxCount, txCount, shared);
}

ZuUnion<void, ZeException> Xsk::open(
  UMem *arena, unsigned ifindex, unsigned queue, unsigned fillCount, unsigned compCount,
  unsigned rxCount, unsigned txCount, int shared)
{
  ZmAssert_(m_fd < 0 && (arena || shared >= 0));
  m_fd = socket(AF_XDP, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (m_fd < 0) return socketFailed("AF_XDP socket");
  auto fail = [this](const char *operation) {
    auto error = socketFailed(operation);
    close();
    return error;
  };
  if (shared < 0) {
    const auto &layout = arena->layout();
    xdp_umem_reg reg{};
    reg.addr = uintptr_t(arena->data());
    reg.len = layout.length;
    reg.chunk_size = layout.chunk;
    reg.headroom = layout.prefix - XDP_PACKET_HEADROOM;
    reg.tx_metadata_len = layout.txMetadata;
    if (reg.tx_metadata_len) reg.flags = XDP_UMEM_TX_METADATA_LEN;
    auto unsupported = [] { return errno == EINVAL || errno == EOPNOTSUPP ||
      errno == ENOPROTOOPT; };
    int rc = setsockopt(m_fd, SOL_XDP, XDP_UMEM_REG, &reg, sizeof(reg));
    if (rc && reg.tx_metadata_len && unsupported()) {
      // The original metadata ABI predates the explicit registration flag.
      reg.flags = 0;
      rc = setsockopt(m_fd, SOL_XDP, XDP_UMEM_REG, &reg, sizeof(reg));
      if (rc && unsupported()) {
	reg.tx_metadata_len = 0;
	rc = setsockopt(m_fd, SOL_XDP, XDP_UMEM_REG, &reg, sizeof(reg));
      }
    }
    if (rc) return fail("AF_XDP UMEM registration");
    m_txMetadata = reg.tx_metadata_len != 0;
  }
  struct { unsigned option, count; } rings[] = {
    {XDP_UMEM_FILL_RING, fillCount}, {XDP_UMEM_COMPLETION_RING, compCount},
    {XDP_RX_RING, rxCount}, {XDP_TX_RING, txCount}
  };
  for (auto ring : rings)
    if (ring.count &&
	setsockopt(m_fd, SOL_XDP, ring.option, &ring.count, sizeof(ring.count)))
      return fail("AF_XDP ring configuration");
  // Linux's default generic Tx budget is 32. The optional override must
  // be at least that large and fit the configured descriptor ring.
  enum { DefaultTxBudget = 32 };
  if (txCount > DefaultTxBudget && setsockopt(m_fd, SOL_XDP, XDP_MAX_TX_SKB_BUDGET,
	&txCount, sizeof(txCount)) && errno != ENOPROTOOPT)
    return fail("AF_XDP Tx budget configuration");
  xdp_mmap_offsets offsets{};
  socklen_t length = sizeof(offsets);
  if (getsockopt(m_fd, SOL_XDP, XDP_MMAP_OFFSETS, &offsets, &length))
    return fail("AF_XDP ring offset query");
  if (length < sizeof(offsets)) {
    close();
    return ZeEXCEPT(Error, "Zpkt", "AF_XDP kernel lacks required ring offset/flags ABI");
  }
  auto mapped = fillCount ? map(0, m_fill, offsets.fr, fillCount, XDP_UMEM_PGOFF_FILL_RING) :
    map(2, m_rx, offsets.rx, rxCount, XDP_PGOFF_RX_RING);
  if (mapped.is<void>() && compCount)
    mapped = map(1, m_comp, offsets.cr, compCount, XDP_UMEM_PGOFF_COMPLETION_RING);
  if (mapped.is<void>() && txCount)
    mapped = map(3, m_tx, offsets.tx, txCount, XDP_PGOFF_TX_RING);
  if (mapped.is<ZeException>()) { close(); return mapped; }

  sockaddr_xdp address{};
  address.sxdp_family = AF_XDP;
  address.sxdp_ifindex = ifindex;
  address.sxdp_queue_id = queue;
  if (shared >= 0) {
    address.sxdp_flags = XDP_SHARED_UMEM;
    address.sxdp_shared_umem_fd = shared;
  } else address.sxdp_flags = XDP_USE_NEED_WAKEUP;
  if (::bind(m_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)))
    return fail("AF_XDP interface/queue bind");

  xdp_options options{};
  length = sizeof(options);
  if (getsockopt(m_fd, SOL_XDP, XDP_OPTIONS, &options, &length))
    return fail("AF_XDP capability query");
  if (length < sizeof(options)) {
    close();
    return ZeEXCEPT(Error, "Zpkt", "AF_XDP kernel lacks capability flags");
  }
  m_zeroCopy = options.flags & XDP_OPTIONS_ZEROCOPY;
  return {};
}

void Xsk::close()
{
  m_txMetadata = false;
  m_rx.freeze(); m_fill.freeze(); m_tx.freeze(); m_comp.freeze();
  m_rx.reset();
  m_fill.reset();
  m_tx.reset();
  m_comp.reset();
  for (unsigned i = 4; i; --i) {
    auto &mapping = m_maps[i - 1];
    if (mapping.addr) munmap(mapping.addr, mapping.length);
    mapping = {};
  }
  if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
  m_zeroCopy = false;
}

} // namespace Zpkt::Xdp
