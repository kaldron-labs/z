//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Device-owned mbuf pools and setup-time packet address indexing.

#ifndef ZpktDpdkPool_HH
#define ZpktDpdkPool_HH

#ifndef ZpktDpdkLib_HH
#include "ZpktDpdkLib.hh"
#endif

#include <stdint.h>

#include <rte_mbuf.h>

#include <zlib/ZuSpan.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiIOBuf.hh>

#ifdef ZPKT_DPDK_TEST
namespace Zpkt::Dpdk::ReturnPoint {
enum { Published = 1, BeforeClear, AfterClear, AfterRecheck, QueuePush, QueuePop, Visit, Wait };
}
extern "C" void ZpktTestDpdkReturns(unsigned point, unsigned count);
#endif

namespace Zpkt::Dpdk {

class ZpktDPDKAPI BufPool;

// The PMD receives the sole native reference. Caller and submission leases
// independently protect the slot from application reuse.
struct ZpktDPDKAPI Slot {
  enum { Caller = 1, Submitted = 2 };

  BufPool	*pool;
  rte_mbuf	*mbuf;
  uint8_t	*packet;
  ZmAtomic<unsigned> leases{0};

  bool submit() { return leases.cmpXch(Caller | Submitted, Caller) == Caller; }
  void release(unsigned lease);
};

// One ready queue per Nif. A queued pool owns its notification claim until
// its returns are drained or the claim is transferred to the queue tail.
class ZpktDPDKAPI TxReturns {
public:
  TxReturns() = default;
  ~TxReturns();
  TxReturns(const TxReturns &) = delete;
  TxReturns &operator =(const TxReturns &) = delete;

  int init(unsigned count, int socket);
  void reset();
  void enter() { ++m_active; }
  void leave() { --m_active; }
  bool active() const { return bool(m_active); }
  void push(rte_mempool *);
  rte_mempool *shift();
  void settle(rte_mempool *);

private:
  rte_ring	*m_ready = nullptr;
  alignas(Zm::CacheLineSize) ZmAtomic<unsigned> m_active{0};
};

// Each populated 4 KiB page indexes its packet start addresses by cache
// line and records the preceding slot if it crosses the page boundary.
class ZpktDPDKAPI AddressIndex {
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

  using Pages = ZtArray<Page, ZtArrayHeapID<"Zpkt.Dpdk.Pages">>;
  using Slots = ZtArray<Slot *, ZtArrayHeapID<"Zpkt.Dpdk.Slots">>;
  Pages		m_pages;
  Slots		m_slots;
  unsigned	m_shift = 0;
};

class ZpktDPDKAPI BufPool final : public ZmNHeap<BufPool>, public Zi::IOBufPool {
friend inline ZuStringT<"Zpkt.DpdkPool"> ZmNHeapID(BufPool *);
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
    int init(BufPool &, int socket, unsigned dataSize, TxReturns *);
    Slot *find(const void *packet) const { return m_index.find(packet); }

  private:
    void reset();

    rte_mempool	*m_pool = nullptr;
    bool	m_tx = false;
    AddressIndex m_index;
  };

  BufPool(
    Zi::IOBufType::T, unsigned count, unsigned size,
    unsigned headRoom, unsigned tailRoom, unsigned align);
  ~BufPool() override;

  int prepare(Prepared &, int socket, unsigned minDataSize = 0, TxReturns * = nullptr);
  void publish(Prepared &&);
  bool final();

  rte_mempool *native() const { return m_backing.native(); }
  static Slot *slot(rte_mbuf *mbuf) { return static_cast<Slot *>(rte_mbuf_to_priv(mbuf)); }
  static void resetTx(rte_mbuf *mbuf) {
    rte_mbuf_refcnt_set(mbuf, 1);
    mbuf->ol_flags = 0;
    rte_pktmbuf_reset(mbuf);
  }
  unsigned available() const;
  void returned(ZmFn<void()> fn) { m_returned = ZuMv(fn); }
  bool adopt(rte_mbuf *);

  uint8_t *alloc() override;
  void free(uint8_t *) override;

private:
  void recycle(rte_mbuf *);

  Prepared	m_backing;
  ZmFn<void()>	m_returned;
};

} // namespace Zpkt::Dpdk
#endif /* ZpktDpdkPool_HH */
