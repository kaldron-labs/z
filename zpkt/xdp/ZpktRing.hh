//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private AF_XDP mapped ring views. Each view has exactly one userspace owner.

#ifndef ZpktRing_HH
#define ZpktRing_HH

#ifndef ZpktXdpLib_HH
#include <zlib/ZpktXdpLib.hh>
#endif

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <sys/socket.h>
#include <linux/if_xdp.h>

#include <zlib/ZuID.hh>
#include <zlib/ZuLib.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmPlatform.hh>

#include <zlib/ZtcQueue.hh>

namespace Zpkt::Xdp {

using RingQueueType = Ztc::QueueType::T;

// Commit publishes a reservation, or its initialized prefix for a producer.
// Cancellation publishes nothing. Publication never retires packet backing.
template <typename Entry, bool Producer> class Ring : public Ztc::Queue {
public:
  using Atomic = ZmAtomic<uint32_t>;
  ZuAssert(sizeof(Atomic) == sizeof(uint32_t));
  Ring() = default;
  Ring(const Ring &) = delete;
  Ring &operator =(const Ring &) = delete;

  void identify(ZuID owner, ZuID id, Ztc::QueueType::T type, int fd = -1) {
    m_ownerID = owner; m_id = id; m_type = type; m_fd = fd;
  }
  TelKey telKey() const override { return {m_ownerID, m_id, m_type}; }
  void telemetry(Ztc::QueueTelemetry &data) const override {
    auto producer = m_finalProd, consumer = m_finalCons;
    if (!m_frozen && m_count) {
      consumer = *m_cons;
      producer = *m_prod;
    }
    data = {};
    data.ownerID = m_ownerID; data.id = m_id; data.type = m_type;
    // The kernel ABI exposes wrapping 32-bit descriptor positions, not byte
    // throughput. A concurrent snapshot may span more than one owner turn.
    data.inCount = producer; data.outCount = consumer;
    auto pending = unsigned(producer - consumer);
    data.count = pending > m_size ? m_size : pending;
    data.size = m_size;
    data.full = m_full;
    if constexpr (!Producer && ZuIsSame<Entry, xdp_desc>{})
      data.full = m_frozen ? m_kernelFull : kernelFull();
  }
  // Capture before closing the socket; retained views need no live mapping.
  void freeze() {
    if (m_frozen) return;
    if (m_count) {
      m_finalCons = *m_cons;
      m_finalProd = *m_prod;
    }
    if constexpr (!Producer && ZuIsSame<Entry, xdp_desc>{})
      m_kernelFull = kernelFull();
    m_frozen = true;
  }

  static bool mapLength(
    const xdp_ring_offset &off, unsigned count, size_t &length)
  {
    if (!count || (count & (count - 1)) || count > (unsigned(1) << 31))
      return false;
    uint64_t begin[] = {off.producer, off.consumer, off.flags, off.desc};
    uint64_t bytes[] = {4, 4, 4, uint64_t(count) * sizeof(Entry)};
    unsigned align[] = {4, 4, 4, alignof(Entry)};
    size_t end = 0;
    for (unsigned i = 0; i < 4; ++i) {
      if (begin[i] % align[i] || begin[i] > SIZE_MAX - bytes[i]) return false;
      auto n = size_t(begin[i] + bytes[i]);
      if (n > end) end = n;
      for (unsigned j = 0; j < i; ++j)
        if (begin[i] < begin[j] + bytes[j] && begin[j] < n) return false;
    }
    length = end;
    return true;
  }

  // The caller owns mmap/munmap and must keep the mapping alive until reset.
  // Attachment is serialized against both peers (normally before socket bind).
  bool init(void *mapping, size_t length, const xdp_ring_offset &off, unsigned count)
  {
    size_t needed;
    if (m_count || !mapping || uintptr_t(mapping) % alignof(Entry) ||
	!mapLength(off, count, needed) || length < needed) return false;
    auto base = static_cast<uint8_t *>(mapping);
    auto prod = reinterpret_cast<Atomic *>(base + off.producer);
    auto cons = reinterpret_cast<Atomic *>(base + off.consumer);
    auto p = uint32_t(*prod);
    auto c = uint32_t(*cons);
    if (unsigned(p - c) > count) return false;
    m_prod = prod;
    m_cons = cons;
    m_flags = reinterpret_cast<Atomic *>(base + off.flags);
    m_entries = reinterpret_cast<Entry *>(base + off.desc);
    m_count = count;
    m_size = count;
    m_finalProd = p; m_finalCons = c;
    m_full = m_kernelFull = 0;
    m_frozen = false;
    m_cursor = Producer ? p : c;
    m_peer = Producer ? c : p;
    return true;
  }

  void reset() {
    ZmAssert_(!m_pending);
    freeze();
    m_prod = m_cons = m_flags = nullptr;
    m_entries = nullptr;
    m_count = m_cursor = m_peer = 0;
  }

  unsigned count() const { return m_count; }
  // May be read by the dedicated sender; it never touches owner-cached state.
  uint32_t consumed() const { return m_count ? uint32_t(*m_cons) : 0; }
  unsigned published() const {
    if (!m_count) return 0;
    auto consumer = uint32_t(*m_cons);
    auto producer = uint32_t(*m_prod);
    return unsigned(producer - consumer);
  }
  unsigned available() {
    if (!m_count) return 0;
    // Acquire pairs with the peer's release of descriptors/slots.
    if constexpr (Producer) {
      m_peer = *m_cons;
      return m_count - unsigned(m_cursor - m_peer);
    } else {
      m_peer = *m_prod;
      return unsigned(m_peer - m_cursor);
    }
  }

  unsigned reserve(unsigned n) {
    ZmAssert_(!m_pending);
    if (!m_count || !n) return 0;
    if constexpr (Producer) {
      if (n > m_count) { ++m_full; return 0; }
    } else if (n > m_count) n = m_count;
    unsigned available;
    if constexpr (Producer) available = m_count - unsigned(m_cursor - m_peer);
    else available = unsigned(m_peer - m_cursor);
    if (available < n) available = this->available();
    if constexpr (Producer) {
      if (available < n) { ++m_full; return 0; }
    } else if (n > available) n = available;
    m_pending = n;
    return n;
  }

  template <bool P = Producer, typename = ZuIfT<P && Producer>>
  unsigned reserveUpTo(unsigned limit) {
    ZmAssert_(!m_pending);
    if (!m_count || !limit) return 0;
    auto space = m_count - unsigned(m_cursor - m_peer);
    if (space < limit) space = available();
    if (space < limit) { ++m_full; limit = space; }
    m_pending = limit;
    return limit;
  }

  // A consumer cannot change a published descriptor. A producer must initialize
  // every committed entry; wraparound is handled by this index.
  decltype(auto) entry(unsigned i) {
    ZmAssert_(i < m_pending);
    auto &entry = m_entries[(m_cursor + i) & (m_count - 1)];
    if constexpr (Producer) return (entry);
    else return static_cast<const Entry &>(entry);
  }

  void commit() { commit_(m_pending); }

  template <bool P = Producer, typename = ZuIfT<P && Producer>>
  void commit(unsigned n) {
    ZmAssert_(n <= m_pending);
    commit_(n);
  }
  void cancel() { m_pending = 0; }

  bool needWakeup() const {
    return m_flags &&
      (m_flags->load_() & XDP_RING_NEED_WAKEUP);
  }
  bool needsKick() const { return published() && needWakeup(); }

private:
  void commit_(unsigned n) {
    m_pending = 0;
    if (!n) return;
    m_cursor += n;
    if constexpr (Producer) *m_prod = m_cursor;
    else *m_cons = m_cursor;
  }

  uint32_t kernelFull() const {
    if (m_fd < 0) return 0;
    xdp_statistics stats{};
    socklen_t length = sizeof(stats);
    int saved = errno;
    bool ok = !getsockopt(m_fd, SOL_XDP, XDP_STATISTICS, &stats, &length);
    errno = saved;
    if (!ok || length < offsetof(xdp_statistics, rx_ring_full) + sizeof(stats.rx_ring_full))
      return 0;
    return stats.rx_ring_full > UINT32_MAX ? UINT32_MAX : uint32_t(stats.rx_ring_full);
  }
  ZuID		m_ownerID, m_id;
  RingQueueType	m_type = -1;
  int		m_fd = -1;
  unsigned	m_size = 0;
  uint32_t	m_finalProd = 0, m_finalCons = 0, m_kernelFull = 0;
  bool		m_frozen = false;
  Atomic		*m_prod = nullptr;
  Atomic		*m_cons = nullptr;
  Atomic		*m_flags = nullptr;
  Entry		*m_entries = nullptr;
  unsigned	m_count = 0;
  // The dedicated sender reads the Tx mapping while its producer advances.
  // Other ring views have one userspace owner and keep their compact layout.
  alignas(Producer && ZuIsSame<Entry, xdp_desc>{} ? Zm::CacheLineSize : alignof(uint32_t))
  uint32_t	m_cursor = 0;
  uint32_t	m_peer = 0;
  unsigned	m_pending = 0;
  uint32_t	m_full = 0; // owner-only failed capacity reservations
};

using RxRing = Ring<xdp_desc, false>;
using TxRing = Ring<xdp_desc, true>;
using FillRing = Ring<uint64_t, true>;
using CompRing = Ring<uint64_t, false>;

} // namespace Zpkt::Xdp

#endif /* ZpktRing_HH */
