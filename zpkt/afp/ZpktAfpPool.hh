//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// AF_PACKET pool geometry, packet leases and transactional provisioning.

#ifndef ZpktAfpPool_HH
#define ZpktAfpPool_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>

namespace Zpkt::Afp {

class BufPool;

struct Slot {
  enum { Caller = 1, Submitted = 2 };

  BufPool	*pool;
  uintptr_t	next;
  ZmAtomic<unsigned> leases{0};

  bool submit() { return leases.cmpXch(Caller | Submitted, Caller) == Caller; }
  void release(unsigned lease, bool notify = true);
  uint8_t *packet();
};

class BufPool final : public ZmNHeap<BufPool>, public Zi::IOBufPool {
friend inline ZuStringT<"Zpkt.Afp.Pool"> ZmNHeapID(BufPool *);
friend struct Slot;

public:
  using ZmNHeap<BufPool>::operator new;
  using ZmNHeap<BufPool>::operator delete;

  struct Geometry {
    size_t	prefix = 0, backing = 0, stride = 0, alignment = 0, bytes = 0;
  };

  class Prepared {
  public:
    Prepared() = default;
    Prepared(Prepared &&) noexcept;
    Prepared &operator =(Prepared &&) noexcept;
    ~Prepared();

    int init(BufPool &);
    uintptr_t begin() const { return uintptr_t(m_slab) + m_geometry.prefix; }
    uintptr_t end() const { return uintptr_t(m_slab) + m_geometry.bytes; }
    const Geometry &geometry() const { return m_geometry; }
    Slot *at(unsigned i) const { return reinterpret_cast<Slot *>(m_slab + i * m_geometry.stride); }
    explicit operator bool() const { return m_slab; }

  private:
    Geometry	m_geometry;
    uint8_t	*m_slab = nullptr;
  };

  BufPool(
    Zi::IOBufType::T, unsigned count, unsigned size,
    unsigned headRoom, unsigned tailRoom, unsigned align);
  ~BufPool() override;

  int prepare(Prepared &storage) { return storage.init(*this); }
  void publish(Prepared &&);
  void final();

  uint8_t *alloc() override;
  void free(uint8_t *) override;
  void discard(uint8_t *);
  Slot *find(const void *) const;
  const Geometry &geometry() const { return m_storage.geometry(); }
  bool hasFree() const { return uintptr_t(m_head); }
  unsigned used() const { return unsigned(m_used); }
  bool allocated() const { return bool(m_storage); }
  void returned(ZmFn<void()> fn) { m_returned = ZuMv(fn); }

private:
  void recycle(Slot *, bool notify);

  Prepared			m_storage;
  ZmFn<void()>			m_returned;
  alignas(Zm::CacheLineSize) ZmAtomic<uintptr_t> m_head{0};
  ZmAtomic<unsigned>		m_used{0};
};

struct Completion {
  Slot		*slot;
  unsigned	length;
};

class PoolSet {
public:
  using PoolRefs = ZtArray<ZmRef<BufPool>, ZtArrayHeapID<"Zpkt.Afp.PoolRefs">>;
  using Result = ZuUnion<void, ZeException>;

  PoolSet() = default;
  ~PoolSet();
  PoolSet(const PoolSet &) = delete;
  PoolSet &operator =(const PoolSet &) = delete;

  Result alloc(
    Zi::IOBufPool *, const Zi::IOBufPoolList &, const PoolRefs &, unsigned mtu);
  bool final();
  bool allocated() const { return bool(m_rx); }
  BufPool *rx() const { return m_rx.ptr(); }
  unsigned txCapacity() const { return m_capacity; }
  Completion *completions() const { return m_completions; }
  Slot *findTx(const void *) const;

private:
  struct Range {
    uintptr_t	begin, end;
    BufPool	*pool;
  };
  using Index = ZtArray<Range, ZtArrayHeapID<"Zpkt.Afp.AddressIndex">>;

  ZmRef<BufPool>	m_rx;
  PoolRefs		m_tx;
  Index			m_index;
  unsigned		m_appTx = 0, m_capacity = 0;
  Completion		*m_completions = nullptr;
};

} // namespace Zpkt::Afp
#endif /* ZpktAfpPool_HH */
