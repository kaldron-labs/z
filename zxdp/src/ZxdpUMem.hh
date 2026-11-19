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

#include <zlib/ZePlatform.hh>

#include <zlib/Zxdp.hh>
#include <zlib/ZxdpRing.hh>
#include <zlib/ZxdpPool.hh>

namespace Zxdp::Xdp {

inline unsigned power2(unsigned n)
{
  if (n > (unsigned(1)<<31)) return 0;
  if (n < 2) return 2;
  return unsigned(1)<<(32 - __builtin_clz(n - 1));
}

struct ArenaLayout {
  size_t length = 0, rxLength = 0, txLength = 0;
  unsigned chunk = 0, prefix = 0, rxCount = 0, txCount = 0;
};

// Final allocation returns can race producer/CQ work on neighboring slots.
struct alignas(Zm::CacheLineSize) TxSlot {
  ZmAtomic<unsigned> refs{0}; // one allocation lease plus any kernel submission
  unsigned wire, active;
  bool pending = false;
};

class ZxdpAPI UMem : public ZmNHeap<UMem>, public ZmObject {
friend inline ZuStringT<"Zxdp.UMem"> ZmNHeapID(UMem *);

public:
  ~UMem();
  static ZuUnion<ZmRef<UMem>, ZeException> create(
    ZuSpan<const ZmRef<Zi::IOBufPool>>, Zi::IOBufPool *rx, unsigned mtu);
  void publish(ZuSpan<const ZmRef<Zi::IOBufPool>>, BufPool *rx);
  uint8_t *data() const { return m_data; }
  const ArenaLayout &layout() const { return m_layout; }
  BufPool *pool(const void *) const;
  TxSlot &txSlot(const BufPool &, unsigned slot);
  void retire(const Zi::IOBufPoolList &, BufPool *rx);

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
public:
  Xsk() = default;
  ~Xsk();
  Xsk(const Xsk &) = delete;
  Xsk &operator =(const Xsk &) = delete;

  ZuUnion<void, ZeException> owner(
    UMem *, unsigned ifindex, unsigned queue, unsigned fillCount, unsigned compCount,
    int shared = -1);
  ZuUnion<void, ZeException> service(
    unsigned ifindex, unsigned queue, unsigned rxCount, unsigned txCount, int shared);
  void close();
  int fd() const { return m_fd; }
  bool zeroCopy() const { return m_zeroCopy; }

  RxRing &rx() { return m_rx; }
  FillRing &fill() { return m_fill; }
  TxRing &tx() { return m_tx; }
  CompRing &comp() { return m_comp; }

private:
  ZuUnion<void, ZeException> open(
    UMem *, unsigned ifindex, unsigned queue, unsigned fillCount, unsigned compCount,
    unsigned rxCount, unsigned txCount, int shared);
  struct Mapping { void *addr = nullptr; size_t length = 0; };
  template <typename R> ZuUnion<void, ZeException> map(
    unsigned i, R &, const xdp_ring_offset &, unsigned count, uint64_t offset);

  int		m_fd = -1;
  Mapping	m_maps[4];
  bool		m_zeroCopy = false;
  alignas(Zm::CacheLineSize) RxRing m_rx;
  FillRing		m_fill;
  alignas(Zm::CacheLineSize) TxRing m_tx;
  CompRing		m_comp;
};

} // namespace Zxdp::Xdp

#endif /* ZxdpUMem_HH */
