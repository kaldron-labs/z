//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private AF_XDP mapped ring views. Each view has exactly one userspace owner.

#ifndef ZxdpRing_HH
#define ZxdpRing_HH

#ifndef ZxdpLib_HH
#include <zlib/ZxdpLib.hh>
#endif

#include <stddef.h>
#include <stdint.h>
#include <linux/if_xdp.h>

#include <zlib/ZmAssert.hh>

namespace Zxdp {

// A reservation is bounded and atomic as a vector: commit publishes all entries,
// cancel publishes none. Descriptor publication never retires packet backing.
template <typename Entry, bool Producer> class Ring {
public:
  Ring() = default;
  Ring(const Ring &) = delete;
  Ring &operator =(const Ring &) = delete;

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
    auto prod = reinterpret_cast<uint32_t *>(base + off.producer);
    auto cons = reinterpret_cast<uint32_t *>(base + off.consumer);
    auto p = __atomic_load_n(prod, __ATOMIC_ACQUIRE);
    auto c = __atomic_load_n(cons, __ATOMIC_ACQUIRE);
    if (unsigned(p - c) > count) return false;
    m_prod = prod;
    m_cons = cons;
    m_flags = reinterpret_cast<uint32_t *>(base + off.flags);
    m_entries = reinterpret_cast<Entry *>(base + off.desc);
    m_count = count;
    m_cursor = Producer ? p : c;
    m_peer = Producer ? c : p;
    return true;
  }

  void reset() {
    ZmAssert_(!m_pending);
    m_prod = m_cons = m_flags = nullptr;
    m_entries = nullptr;
    m_count = m_cursor = m_peer = 0;
  }

  unsigned count() const { return m_count; }
  unsigned available() {
    if (!m_count) return 0;
    // Acquire pairs with the peer's release of descriptors/slots.
    if constexpr (Producer) {
      m_peer = __atomic_load_n(m_cons, __ATOMIC_ACQUIRE);
      return m_count - unsigned(m_cursor - m_peer);
    } else {
      m_peer = __atomic_load_n(m_prod, __ATOMIC_ACQUIRE);
      return unsigned(m_peer - m_cursor);
    }
  }

  unsigned reserve(unsigned n) {
    ZmAssert_(!m_pending);
    if (!m_count || !n) return 0;
    if constexpr (Producer) {
      if (n > m_count) return 0;
    } else if (n > m_count) n = m_count;
    unsigned available;
    if constexpr (Producer) available = m_count - unsigned(m_cursor - m_peer);
    else available = unsigned(m_peer - m_cursor);
    if (available < n) available = this->available();
    if constexpr (Producer) {
      if (available < n) return 0;
    } else if (n > available) n = available;
    m_pending = n;
    return n;
  }

  // A consumer cannot change a published descriptor. A producer must initialize
  // every reserved entry before commit; wraparound is handled by this index.
  decltype(auto) entry(unsigned i) {
    ZmAssert_(i < m_pending);
    auto &entry = m_entries[(m_cursor + i) & (m_count - 1)];
    if constexpr (Producer) return (entry);
    else return static_cast<const Entry &>(entry);
  }

  void commit() {
    if (!m_pending) return;
    m_cursor += m_pending;
    m_pending = 0;
    if constexpr (Producer) __atomic_store_n(m_prod, m_cursor, __ATOMIC_RELEASE);
    else __atomic_store_n(m_cons, m_cursor, __ATOMIC_RELEASE);
  }
  void cancel() { m_pending = 0; }

  bool needWakeup() const {
    return m_flags &&
      (__atomic_load_n(m_flags, __ATOMIC_RELAXED) & XDP_RING_NEED_WAKEUP);
  }

private:
  uint32_t	*m_prod = nullptr;
  uint32_t	*m_cons = nullptr;
  uint32_t	*m_flags = nullptr;
  Entry		*m_entries = nullptr;
  unsigned	m_count = 0;
  uint32_t	m_cursor = 0;
  uint32_t	m_peer = 0;
  unsigned	m_pending = 0;
};

using RxRing = Ring<xdp_desc, false>;
using TxRing = Ring<xdp_desc, true>;
using FillRing = Ring<uint64_t, true>;
using CompRing = Ring<uint64_t, false>;

} // namespace Zxdp

#endif /* ZxdpRing_HH */
