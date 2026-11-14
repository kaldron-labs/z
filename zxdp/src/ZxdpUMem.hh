//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private shared packet arena layout, registration and mapping lifetime.

#ifndef ZxdpUMem_HH
#define ZxdpUMem_HH

#ifndef ZxdpLib_HH
#include <zlib/ZxdpLib.hh>
#endif

#include <stddef.h>
#include <stdint.h>

#include <zlib/ZuSpan.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/Zxdp.hh>
#include <zlib/ZxdpRing.hh>

namespace Zxdp {

struct ArenaLayout {
  size_t length = 0, rxLength = 0, txLength = 0;
  unsigned chunk = 0, prefix = 0, rxCount = 0, txCount = 0;
};

struct TxSlot {
  ZmAtomic<unsigned> refs{0}; // one allocation lease plus any kernel submission
  unsigned wire, active;
  bool pending = false;
};

class ZxdpAPI UMem : public ZmNHeap<UMem>, public ZmObject {
friend inline ZuStringT<"Zxdp.UMem"> ZmNHeapID(UMem *);

public:
  ~UMem();
  static ZmRef<UMem> create(const Params &, unsigned mtu, ZtString<> &error);
  uint8_t *data() const { return m_data; }
  const ArenaLayout &layout() const { return m_layout; }
  BufPool *pool(const void *) const;
  TxSlot &txSlot(const BufPool &, unsigned slot);
  void retire(BufPool *, BufPool *rx);

private:
  UMem() = default;
  using Pages = ZtArray<BufPool *, ZtArrayHeapID<"Zxdp.Pages">>;
  using Slots = ZtArray<TxSlot, ZtArrayHeapID<"Zxdp.TxSlots">>;
  uint8_t	*m_data = nullptr;
  ArenaLayout	m_layout;
  Pages		m_pages;
  Slots		m_tx;
  unsigned	m_pageShift = 0;
};

// Private kernel registration/binding. Quiesce Rx/Tx service before close.
// Descriptor mappings never own application packet lifetime.
// Handle closure alone does not prove retirement of outstanding device DMA.
class ZxdpAPI Xsk {
  friend class KernelRetirement;

public:
  Xsk() = default;
  ~Xsk();
  Xsk(const Xsk &) = delete;
  Xsk &operator =(const Xsk &) = delete;

  bool open(
    ZmRef<UMem>, unsigned ifindex, unsigned queue, unsigned rxCount, unsigned txCount,
    ZtString<> &error, int shared = -1);
  bool prepare(
    ZmRef<UMem>, unsigned rxCount, unsigned txCount, ZtString<> &error, int shared = -1);
  // EBUSY preserves this prepared registration for owner-directed bind retry.
  bool bind(unsigned ifindex, unsigned queue, ZtString<> &error);
  bool retryBind() const { return m_retryBind; }
  void close();
  int fd() const { return m_fd; }
  bool zeroCopy() const { return m_zeroCopy; }
  bool bound() const { return m_bound; }

  RxRing &rx() { return m_rx; }
  FillRing &fill() { return m_fill; }
  TxRing &tx() { return m_tx; }
  CompRing &comp() { return m_comp; }

private:
  UMem *arena() const { return m_arena; }
  uint64_t namespaceID() const { return m_namespace; }
  unsigned ifindex() const { return m_ifindex; }
  unsigned queue() const { return m_queue; }
  bool bind_(unsigned ifindex, unsigned queue, ZtString<> &error, bool queryOptions);
  struct Mapping { void *addr = nullptr; size_t length = 0; };
  template <typename R> bool map(
    unsigned i, R &, const xdp_ring_offset &, unsigned count, uint64_t offset,
    ZtString<> &error);

  int		m_fd = -1;
  int		m_shared = -1;
  ZmRef<UMem>	m_arena;
  Mapping	m_maps[4];
  uint64_t	m_namespace = 0;
  unsigned	m_ifindex = 0;
  unsigned	m_queue = 0;
  bool		m_zeroCopy = false;
  bool		m_bound = false;
  bool		m_retryBind = false;
  alignas(Zm::CacheLineSize) RxRing m_rx;
  FillRing		m_fill;
  alignas(Zm::CacheLineSize) TxRing m_tx;
  CompRing		m_comp;
};

} // namespace Zxdp

#endif /* ZxdpUMem_HH */
