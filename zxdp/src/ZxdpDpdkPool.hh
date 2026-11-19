//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Device-owned mbuf pools and setup-time packet address indexing.

#ifndef ZxdpDpdkPool_HH
#define ZxdpDpdkPool_HH

#ifndef ZxdpLib_HH
#include <zlib/ZxdpLib.hh>
#endif

#include <stdint.h>

#include <rte_mbuf.h>

#include <zlib/ZuSpan.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiIOBuf.hh>

namespace Zxdp::Dpdk {

class BufPool;

// One guard ref protects the native slot while either lease is held. Only
// the PMD's submission uses a second native ref, so even a retained caller
// lease cannot hide the moment the PMD gives up its own ref.
struct Slot {
  enum { Caller = 1, Submitted = 2 };

  BufPool	*pool;
  rte_mbuf	*mbuf;
  uint8_t	*packet;
  ZmAtomic<unsigned> leases{0};

  bool submit();
  bool retired() const;
  void releaseSubmission();
  void release(unsigned lease);
};

// Each populated 4 KiB page indexes its packet start addresses by cache
// line and records the preceding slot if it crosses the page boundary.
class AddressIndex {
public:
  AddressIndex() = default;
  AddressIndex(AddressIndex &&) noexcept;
  AddressIndex &operator =(AddressIndex &&) noexcept;
  AddressIndex(const AddressIndex &) = delete;
  AddressIndex &operator =(const AddressIndex &) = delete;

  int init(ZuSpan<rte_mempool *const>);
  void reset();
  Slot *find(const void *) const;

private:
  struct Page {
    uintptr_t	key = 0;
    uint64_t	starts = 0;
    Slot	*carry = nullptr;
    unsigned	first;
  };

  unsigned page(uintptr_t) const;

  using Pages = ZtArray<Page, ZtArrayHeapID<"Zxdp.Dpdk.Pages">>;
  using Slots = ZtArray<Slot *, ZtArrayHeapID<"Zxdp.Dpdk.Slots">>;
  Pages		m_pages;
  Slots		m_slots;
  unsigned	m_shift = 0;
};

class BufPool final : public ZmNHeap<BufPool>, public Zi::IOBufPool {
friend inline ZuStringT<"Zxdp.DpdkPool"> ZmNHeapID(BufPool *);
friend struct Slot;

public:
  using ZmNHeap<BufPool>::operator new;
  using ZmNHeap<BufPool>::operator delete;
  using Slot = Dpdk::Slot;

  class Prepared {
  public:
    Prepared() = default;
    Prepared(Prepared &&) noexcept;
    ~Prepared();
    Prepared &operator =(Prepared &&) noexcept;
    Prepared(const Prepared &) = delete;
    Prepared &operator =(const Prepared &) = delete;

    rte_mempool *native() const { return m_pool; }
    int init(BufPool &, int socket, unsigned dataSize);
    Slot *find(const void *packet) const { return m_index.find(packet); }

  private:
    rte_mempool	*m_pool = nullptr;
    AddressIndex m_index;
  };

  BufPool(
    Zi::IOBufType::T, unsigned count, unsigned size,
    unsigned headRoom, unsigned tailRoom, unsigned align);
  ~BufPool() override;

  int prepare(Prepared &, int socket, unsigned minDataSize = 0);
  void publish(Prepared &&);
  bool final();

  rte_mempool *native() const { return m_backing.native(); }
  static Slot *slot(rte_mbuf *mbuf) { return static_cast<Slot *>(rte_mbuf_to_priv(mbuf)); }
  void returned(ZmFn<void()> fn) { m_returned = ZuMv(fn); }
  bool adopt(rte_mbuf *);

  uint8_t *alloc() override;
  void free(uint8_t *) override;

private:
  void recycle(rte_mbuf *);

  Prepared	m_backing;
  ZmFn<void()>	m_returned;
};

} // namespace Zxdp::Dpdk
#endif /* ZxdpDpdkPool_HH */
