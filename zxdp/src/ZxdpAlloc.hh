//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Caller-owned intrusive packet pools over registered arena regions.

#ifndef ZxdpAlloc_HH
#define ZxdpAlloc_HH

#ifndef ZxdpLib_HH
#include <zlib/ZxdpLib.hh>
#endif

#include <stdint.h>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRef.hh>

namespace Zxdp {

class UMem;
class BufPool;
struct InstanceData;

struct PoolParams {
  BufPool *next = nullptr;
  unsigned count = 0;
  unsigned size = 0;
};

// Keep pool objects alive through instance close and final packet return.
// Freeze the list/count/size while bound. Exhaustion never allocates backing.
class ZxdpAPI BufPool : public ZmNHeap<BufPool>, public PoolParams {
friend inline ZuStringT<"Zxdp.Pool"> ZmNHeapID(BufPool *);
friend class UMem;
friend struct InstanceData;

public:
  enum { NoSlot = unsigned(-1) };
  BufPool(unsigned count = 0, unsigned size = 0);
  ~BufPool();
  BufPool(const BufPool &) = delete;
  BufPool &operator =(const BufPool &) = delete;

  uint8_t *alloc();
  void free(void *packet);
  unsigned used() const { return unsigned(m_used); }
  bool ready() const { return unsigned(m_active); }
  bool owns(const void *) const;
  unsigned slot(const void *) const;
  uint8_t *backing(unsigned slot) const;
  // Tx owner, with the caller allocation lease held. True only after all
  // kernel use retires; permits header changes before a subsequent send.
  bool writable(const void *) const;

private:
  void init(ZmRef<UMem>, uint64_t offset, unsigned stride, unsigned prefix, unsigned first);
  void retire() { m_active = 0; }
  UMem *arena() const { return m_arena; }
  unsigned first() const { return m_first; }
  unsigned stride() const { return unsigned(1)<<m_shift; }
  uint64_t offset() const { return m_offset; }

  using Head = ZmAtomic<uintptr_t>;
  using Count = ZmAtomic<unsigned>;
  ZmRef<UMem>	m_arena;
  uint64_t	m_offset = 0;
  unsigned	m_shift = 0, m_prefix = 0, m_first = 0;
  bool		m_tx = false;
  alignas(Zm::CacheLineSize) Head m_head{0};
  Count		m_used{0}, m_active{0};
};

} // namespace Zxdp
#endif /* ZxdpAlloc_HH */
