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
#include <zlib/ZxdpUMem.hh>

namespace Zxdp {

static unsigned power2(unsigned n)
{
  if (n > (unsigned(1)<<31)) return 0;
  if (n < 2) return 2;
  return unsigned(1)<<(32 - __builtin_clz(n - 1));
}

ZmRef<UMem> UMem::create(const Params &params, unsigned mtu, ZtString<> &error)
{
  error = {};
  auto fail = [&error](const char *text) -> ZmRef<UMem> { error = text; return nullptr; };
  // Linux copy-mode Rx adds NET_IP_ALIGN before the Ethernet header.
  enum { RxAlign = 2 };
  auto rx = params.rxPool;
  auto page = sysconf(_SC_PAGESIZE);
  if (page < 2048 || uint64_t(page) > UINT32_MAX || (page & (page - 1)))
    return fail("unsupported AF_XDP page size");
  if (!rx || !rx->count || rx->next || rx->ready() || rx->used())
    return fail("one unused, nonempty Rx pool is required");
  if (!mtu || rx->size < mtu + 14 || rx->size > unsigned(page) - XDP_PACKET_HEADROOM - RxAlign)
    return fail("Rx packet size does not fit the interface and one AF_XDP chunk");
  // Configuration traversal only; detect cycles before allocating any mapping.
  auto slow = params.txPools, fast = slow;
  while (fast && fast->next) {
    slow = slow->next;
    fast = fast->next->next;
    if (slow == fast) return fail("cyclic Tx pool list");
  }
  ArenaLayout layout;
  layout.prefix = XDP_PACKET_HEADROOM;
  layout.chunk = power2(rx->size + layout.prefix + RxAlign);
  if (layout.chunk < 2048) layout.chunk = 2048;
  layout.rxCount = rx->count;
  uint64_t txBytes = 0;
  for (auto pool = params.txPools; pool; pool = pool->next) {
    if (pool == rx || pool->ready() || pool->used() || !pool->size ||
	pool->size > unsigned(page)) return fail("invalid or busy Tx pool");
    if (pool->count > UINT32_MAX - layout.txCount) return fail("Tx count overflow");
    layout.txCount += pool->count;
    auto stride = power2(pool->size < 64 ? 64 : pool->size);
    if (stride > layout.chunk) layout.chunk = stride;
    auto bytes = uint64_t(pool->count) * stride;
    auto length = (bytes + page - 1) & ~(uint64_t(page) - 1);
    if (length > SIZE_MAX - txBytes) return fail("Tx arena size overflow");
    txBytes += length;
  }
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
    error = "AF_XDP arena mmap failed: ";
    error << strerror(errno);
    return nullptr;
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
  for (auto pool = params.txPools; pool; pool = pool->next) {
    auto stride = power2(pool->size < 64 ? 64 : pool->size);
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
void UMem::retire(BufPool *head, BufPool *rx)
{
  rx->retire();
  for (auto pool = head; pool; pool = pool->next) pool->retire();
}
UMem::~UMem() { if (m_data) munmap(m_data, m_layout.length); }

static bool socketFailed(ZtString<> &error, const char *operation)
{
  auto errorNo = errno;
  error = operation;
  error << ": " << strerror(errorNo);
  return false;
}

template <typename R> bool Xsk::map(
  unsigned i, R &ring, const xdp_ring_offset &off, unsigned count, uint64_t offset,
  ZtString<> &error)
{
  size_t length;
  if (!R::mapLength(off, count, length)) {
    error = "invalid AF_XDP kernel ring offsets or mapping length";
    return false;
  }
  auto mem = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, offset);
  if (mem == MAP_FAILED) return socketFailed(error, "AF_XDP ring mmap");
  m_maps[i] = {mem, length};
  if (!ring.init(mem, length, off, count)) {
    error = "invalid AF_XDP mapped ring state";
    return false;
  }
  return true;
}

Xsk::~Xsk() { close(); }

bool Xsk::open(
  ZmRef<UMem> arena, unsigned ifindex, unsigned queue, unsigned rxCount, unsigned txCount,
  ZtString<> &error, int shared)
{
  if (!prepare(ZuMv(arena), rxCount, txCount, error, shared)) return false;
  if (bind(ifindex, queue, error)) return true;
  close();
  return false;
}

bool Xsk::prepare(
  ZmRef<UMem> arena, unsigned rxCount, unsigned txCount, ZtString<> &error, int shared)
{
  error = {};
  if (m_fd >= 0 || !arena || !rxCount || !txCount ||
      (rxCount & (rxCount - 1)) || (txCount & (txCount - 1))) {
    error = "invalid or already open AF_XDP registration/binding";
    return false;
  }
  m_fd = socket(AF_XDP, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (m_fd < 0) return socketFailed(error, "AF_XDP socket");
  m_arena = ZuMv(arena);
  m_shared = shared;
  auto fail = [this, &error](const char *operation) {
    socketFailed(error, operation);
    close();
    return false;
  };
  socklen_t namespaceLength = sizeof(m_namespace);
  if (getsockopt(m_fd, SOL_SOCKET, SO_NETNS_COOKIE, &m_namespace, &namespaceLength))
    return fail("AF_XDP socket namespace query");
  if (namespaceLength != sizeof(m_namespace)) {
    error = "AF_XDP kernel lacks socket namespace identity";
    close();
    return false;
  }
  const auto &layout = m_arena->layout();
  xdp_umem_reg reg{};
  reg.addr = reinterpret_cast<uintptr_t>(m_arena->data());
  reg.len = layout.length;
  reg.chunk_size = layout.chunk;
  reg.headroom = layout.prefix - XDP_PACKET_HEADROOM;
  if (shared < 0 && setsockopt(m_fd, SOL_XDP, XDP_UMEM_REG, &reg, sizeof(reg)))
    return fail("AF_XDP UMEM registration");
  struct { unsigned option, count; } rings[] = {
    {XDP_UMEM_FILL_RING, rxCount}, {XDP_UMEM_COMPLETION_RING, txCount},
    {XDP_RX_RING, rxCount}, {XDP_TX_RING, txCount}
  };
  for (auto ring : rings)
    if (setsockopt(m_fd, SOL_XDP, ring.option, &ring.count, sizeof(ring.count)))
      return fail("AF_XDP ring configuration");
  xdp_mmap_offsets offsets{};
  socklen_t length = sizeof(offsets);
  if (getsockopt(m_fd, SOL_XDP, XDP_MMAP_OFFSETS, &offsets, &length))
    return fail("AF_XDP ring offset query");
  if (length < sizeof(offsets)) {
    error = "AF_XDP kernel lacks required ring offset/flags ABI";
    close();
    return false;
  }
  if (!map(0, m_fill, offsets.fr, rxCount, XDP_UMEM_PGOFF_FILL_RING, error) ||
      !map(1, m_comp, offsets.cr, txCount, XDP_UMEM_PGOFF_COMPLETION_RING, error) ||
      !map(2, m_rx, offsets.rx, rxCount, XDP_PGOFF_RX_RING, error) ||
      !map(3, m_tx, offsets.tx, txCount, XDP_PGOFF_TX_RING, error)) {
    close();
    return false;
  }
  return true;
}

bool Xsk::bind(unsigned ifindex, unsigned queue, ZtString<> &error)
{
  return bind_(ifindex, queue, error, true);
}

bool Xsk::bind_(unsigned ifindex, unsigned queue, ZtString<> &error, bool queryOptions)
{
  m_retryBind = false;
  if (m_fd < 0 || m_bound || !ifindex || ifindex > INT_MAX) {
    error = "invalid or already bound AF_XDP socket";
    return false;
  }
  sockaddr_xdp address{};
  address.sxdp_family = AF_XDP;
  address.sxdp_ifindex = ifindex;
  address.sxdp_queue_id = queue;
  // Ordinary binding lets the kernel select supported zero-copy operation.
  // Single-buffer packets need neither forced copy nor scatter/gather flags.
  address.sxdp_flags = XDP_USE_NEED_WAKEUP;
  if (m_shared >= 0) {
    address.sxdp_flags = XDP_SHARED_UMEM; // inherits NEED_WAKEUP from the first socket
    address.sxdp_shared_umem_fd = m_shared;
  }
  if (::bind(m_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address))) {
    m_retryBind = errno == EBUSY;
    return socketFailed(error, "AF_XDP interface/queue bind");
  }
  m_bound = true;
  m_ifindex = ifindex;
  m_queue = queue;
  error = {};
  if (!queryOptions) return true;
  xdp_options options{};
  socklen_t length = sizeof(options);
  if (getsockopt(m_fd, SOL_XDP, XDP_OPTIONS, &options, &length)) {
    socketFailed(error, "AF_XDP capability query");
    close();
    return false;
  }
  if (length < sizeof(options)) {
    error = "AF_XDP kernel lacks capability flags";
    close();
    return false;
  }
  m_zeroCopy = options.flags & XDP_OPTIONS_ZEROCOPY;
  error = {};
  return true;
}

void Xsk::close()
{
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

} // namespace Zxdp
