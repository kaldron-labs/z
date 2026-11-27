//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Caller-owned intrusive packet pools over registered arena regions.

#ifndef ZpktPool_HH
#define ZpktPool_HH

#ifndef ZpktXdpLib_HH
#include <zlib/ZpktXdpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZiIOBuf.hh>

namespace Zpkt::Xdp {

class UMem;
class BufPool;
class Nif;

// Keep pool objects alive through instance close and final packet return.
// Freeze the list/count/size while bound. Exhaustion never allocates backing.
class ZpktXDPAPI BufPool final : public ZmNHeap<BufPool>, public Zi::IOBufPool {
friend inline ZuStringT<"Zpkt.Pool"> ZmNHeapID(BufPool *);
friend class UMem;
friend class Nif;
friend struct PoolTest;

public:
  using ZmNHeap<BufPool>::operator new;
  using ZmNHeap<BufPool>::operator delete;
  BufPool(Zi::IOBufType::T, unsigned count, unsigned size);
  ~BufPool() override;
  BufPool(const BufPool &) = delete;
  BufPool &operator =(const BufPool &) = delete;

  uint8_t *alloc() override;
  void free(uint8_t *packet) override;
private:
  enum { NoSlot = unsigned(-1) };

  unsigned used() const { return unsigned(m_used); }
  bool ready() const { return unsigned(m_active); }
  unsigned slot(const void *) const;
  uint8_t *backing(unsigned slot) const;

  void init(ZmRef<UMem>, uint64_t offset, unsigned stride, unsigned prefix, unsigned first);
  void retire() { m_active = 0; }
  bool hasFree() const { return uintptr_t(m_head); }
  void armReturn() { m_armed.xch(1); }
  bool cancelReturn() { return !m_armed.xch(0); } // true if a return claimed the arm
  void returned(ZmFn<void()> fn) { m_returned = ZuMv(fn); }
  void final(unsigned kernelHeld);
  unsigned first() const { return m_first; }
  unsigned stride() const { return unsigned(1)<<m_shift; }

  using Head = ZmAtomic<uintptr_t>;
  using Count = ZmAtomic<unsigned>;
  ZmRef<UMem>	m_arena;
  uint64_t	m_offset = 0;
  unsigned	m_shift = 0, m_prefix = 0, m_first = 0;
  bool		m_tx = false;
  ZmFn<void()>	m_returned;
  alignas(Zm::CacheLineSize) Head m_head{0};
  Count		m_used{0}, m_active{0}, m_armed{0};
};

} // namespace Zpkt::Xdp
#endif /* ZpktPool_HH */
