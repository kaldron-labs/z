//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Caller-owned intrusive packet pools over registered arena regions.

#ifndef ZxdpPool_HH
#define ZxdpPool_HH

#ifndef ZxdpLib_HH
#include <zlib/ZxdpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZiIOBuf.hh>

namespace Zxdp::Xdp {

class UMem;
class BufPool;
class Instance;

// Keep pool objects alive through instance close and final packet return.
// Freeze the list/count/size while bound. Exhaustion never allocates backing.
class BufPool final : public ZmNHeap<BufPool>, public Zi::IOBufPool {
friend inline ZuStringT<"Zxdp.Pool"> ZmNHeapID(BufPool *);
friend class UMem;
friend class Instance;

public:
  using ZmNHeap<BufPool>::operator new;
  using ZmNHeap<BufPool>::operator delete;
  BufPool(unsigned count = 0, unsigned size = 0);
  ~BufPool() override;
  BufPool(const BufPool &) = delete;
  BufPool &operator =(const BufPool &) = delete;

  uint8_t *alloc() override;
  void free(uint8_t *packet) override;
  unsigned used() const override { return unsigned(m_used); }
  bool ready() const override { return unsigned(m_active); }
  bool owns(const void *) const override;
  unsigned slot(const void *) const override;
  uint8_t *backing(unsigned slot) const override;
  // Tx owner, with the caller allocation lease held. True only after all
  // kernel use retires; permits header changes before a subsequent send.
  bool writable(const void *) const override;

private:
  void init(ZmRef<UMem>, uint64_t offset, unsigned stride, unsigned prefix, unsigned first);
  void retire() { m_active = 0; }
  unsigned first() const { return m_first; }
  unsigned stride() const { return unsigned(1)<<m_shift; }

  using Head = ZmAtomic<uintptr_t>;
  using Count = ZmAtomic<unsigned>;
  ZmRef<UMem>	m_arena;
  uint64_t	m_offset = 0;
  unsigned	m_shift = 0, m_prefix = 0, m_first = 0;
  bool		m_tx = false;
  alignas(Zm::CacheLineSize) Head m_head{0};
  Count		m_used{0}, m_active{0};
};

} // namespace Zxdp::Xdp
#endif /* ZxdpPool_HH */
