//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// recycling zero-overhead block allocator
// - compile-time determination of fixed object size
// - intentionally recycles without zero-filling
// - arenas with CPU core and NUMA affinity
// - cache-aware
// - optional partitions / sharding
//   - fast partition lookup
// - shared arena free list
// - efficient statistics and telemetry (Ztc)
// - globally configured
//   - supports profile-guided optimization of heap configuration

// Place ZmNHeap first in the list of base classes for empty base optimization.

#ifndef ZmHeap_HH
#define ZmHeap_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <new>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuString.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmBitmap.hh>
#include <zlib/ZmSpecific.hh>

#include <zlib/ZtcHeap.hh>

#ifdef ZmHeap_TEST
#include <ZmHeapTest.hh>
#endif

#if defined(ZDEBUG) && !defined(ZmHeap_DEBUG)
#define ZmHeap_DEBUG
#endif

// ZmHeapFailFn() should only return true
// - it should call ::abort() if no emergency reserve is available
typedef bool (*ZmHeapFailFn)();
ZmExtern void ZmHeapOnFail(ZmHeapFailFn);

ZmExtern bool ZmHeapFail(); // should only return true, or ::abort()

// Raw heap fallback for scratch storage and sanitizer-visible allocations.
// Free with Zm::alignedFree; nonzero allocation failure follows ZmHeapFail.
template <unsigned Align>
inline void *ZmHeapAlloc(unsigned size) {
  if (ZuUnlikely(!size)) return nullptr;
  void *ptr;
retry:
  if (ZuLikely(ptr = Zm::alignedAlloc<Align>(size))) return ptr;
  if (ZmHeapFail()) goto retry;
  ZuUnreachable();
}

class ZmHeapMgr;
class ZmHeapMgr_;
class ZmHeapCache;
template <
  typename ID,
  unsigned Size,
  unsigned Algnment,
  bool Sharded,
  unsigned VShift>
class ZmHeapCacheT;

struct ZmHeapConfig {
  uint64_t	cacheSize;
  ZmBitmap	cpuset;
};

struct ZmHeapInfo {
  ZuCSpan	id;
  ZmHeapConfig	config;
  uint32_t	size;
  uint16_t	partition;
  uint16_t	alignment;
  uint8_t	vshift;
  bool		sharded;
};

// per-ID heap stats
struct ZmHeapGlobalStats {
  ZmAtomic<uint64_t>	heapAllocs;
  ZmAtomic<uint64_t>	heapFrees;
  ZmAtomic<uint64_t>	heapMax;
};

// per-arena stats: ID, partition, size, alignment and sharding
struct ZmHeapStats {
  ZmAtomic<uint64_t>	allocs;
  ZmAtomic<uint64_t>	heapAllocs;
  ZmAtomic<uint64_t>	frees;
  ZmAtomic<uint64_t>	crossFrees;
  ZmAtomic<uint64_t>	heapFrees;
};

class ZmHeapLookup;

// cache (LIFO free list) of fixed-size blocks; one per CPU set / NUMA node
class ZmAPI ZmHeapCache final : public ZmObject, public Ztc::Heap {
friend ZmHeapMgr;
friend ZmHeapMgr_;
friend ZmHeapLookup;
#ifdef ZmHeap_TEST
friend ZmHeapTest;
#endif
template <typename, unsigned, unsigned, bool, unsigned>
friend class ZmHeapCacheT;

  void *operator new(size_t s);
  void *operator new(size_t s, void *p);
public:
  void operator delete(void *p);

private:
  ZmHeapCache(
      ZuCSpan id, uint16_t partition, uint8_t vshift,
      const ZmHeapConfig &,
      uint32_t size, uint16_t alignment, bool sharded,
      ZmHeapGlobalStats *, hwloc_topology_t);

  void lookup(ZmHeapLookup *l) { m_lookup = l; }
  ZmHeapLookup *lookup() const { return m_lookup; }

public:
  ~ZmHeapCache();

  const ZmHeapInfo &info() const { return m_info; }
  void *begin() const { return m_begin; }
  void *end() const { return m_end; }
  const ZmHeapGlobalStats &globalStats() const { return *m_globalStats; }
  const ZmHeapStats &stats() const { return m_stats; }

  static const auto &IDAxor(const ZmHeapCache *this_) {
    return this_->info().id;
  }
  using IDSize = ZuTuple<ZuCSpan, unsigned>;
  // primary key for a heap is {ID, partition, size, alignment, sharded}
  using Key = ZuTuple<ZuCSpan, unsigned, unsigned, unsigned, bool>;
  static Key KeyAxor(const ZmHeapCache *this_) {
    const auto &info = this_->info();
    return {info.id, info.partition, info.size, info.alignment, info.sharded};
  }

  void warmup();

  TelKey telKey() const override;
  void telemetry(Ztc::HeapTelemetry &data) const override;

#ifdef ZmHeap_DEBUG
  typedef void (*TraceFn)(ZuCSpan, unsigned);
#endif

private:
  bool init(const ZmHeapConfig &, hwloc_topology_t);
  bool init_(hwloc_topology_t);
  void publish_() {
#ifdef ZmHeap_TEST
    ZmHeapTest::hook(ZmHeapTest::Registered, this);
#endif
    m_head = reinterpret_cast<uintptr_t>(m_begin); // release publication
#ifdef ZmHeap_TEST
    ZmHeapTest::hook(ZmHeapTest::Published, this);
#endif
  }
  void final_();

  template <unsigned Align>
  void *alloc() {
#ifdef ZmHeap_DEBUG
    {
      TraceFn fn;
      if (ZuUnlikely(fn = m_traceAllocFn)) (*fn)(m_info.id, m_info.size);
    }
#endif
    void *ptr;
    if (ZuLikely(ptr = alloc_())) return ptr;
  retry:
    if (ZuLikely(ptr = Zm::alignedAlloc<Align>(m_info.size))) {
      ++m_stats.heapAllocs;
      uint64_t allocd = ++m_globalStats->heapAllocs - m_globalStats->heapFrees.load_();
      m_globalStats->heapMax.maximum(allocd);
      return ptr;
    }
    if (ZmHeapFail()) goto retry;
    ZuUnreachable();
  }

  void free(void *p);

  // lock-free MPMC LIFO slist

  void *alloc_() {
    uintptr_t p;
  loop:
    p = m_head.load_();
    if (ZuUnlikely(!p)) return nullptr;
    if (ZuLikely(m_info.sharded)) { // sharded - no contention
      ++m_stats.allocs;
      m_head.store_(*reinterpret_cast<uintptr_t *>(p));
      return reinterpret_cast<void *>(p);
    }
    if (ZuUnlikely(p & 1)) { ZmAtomic_acquire(); goto loop; }
    if (ZuUnlikely(m_head.cmpXch(p | 1, p) != p)) goto loop;
    ++m_stats.allocs;
    m_head = reinterpret_cast<ZmAtomic<uintptr_t> *>(p)->load_();
    return reinterpret_cast<void *>(p);
  }
  void free_(void *p) {
    ++m_stats.frees;
    uintptr_t n;
  loop:
    n = m_head.load_();
    if (n & 1) { ZmAtomic_acquire(); goto loop; }
    reinterpret_cast<ZmAtomic<uintptr_t> *>(p)->store_(n);
    if (m_head.cmpXch(reinterpret_cast<uintptr_t>(p), n) != n) goto loop;
  }
  void free_sharded(void *p) { // sharded - no contention
    ++m_stats.frees;
    *reinterpret_cast<uintptr_t *>(p) = m_head.load_();
    m_head.store_(reinterpret_cast<uintptr_t>(p));
  }

  bool owned(void *p) const {
    auto begin = m_begin;
#ifdef ZmHeap_TEST
    ZmHeapTest::hook(ZmHeapTest::Owned, this, begin);
#endif
    return begin && p >= begin && p < m_end;
  }

  alignas(Zm::CacheLineSize)
  ZmHeapInfo			m_info;
  ZmHeapGlobalStats		*m_globalStats = nullptr;

  // begin, end, lookup are guarded by ZmHeapMgr
  void				*m_begin = nullptr;	// bound memory region
  void				*m_end = nullptr;	// end of memory region
  ZmHeapLookup			*m_lookup = nullptr;

#ifdef ZmHeap_DEBUG
  void traceAllocFn(TraceFn fn) { m_traceAllocFn = fn; }
  void traceFreeFn(TraceFn fn) { m_traceFreeFn = fn; }

  TraceFn			m_traceAllocFn = nullptr;
  TraceFn			m_traceFreeFn = nullptr;
#endif

  alignas(Zm::CacheLineSize)
  ZmAtomic<uintptr_t>		m_head;		// free list (contended atomic)
  mutable ZmHeapStats		m_stats;	// statistics
};

class ZmAPI ZmHeapMgr {
friend ZmHeapCache;
template <typename, unsigned, unsigned, bool, unsigned>
friend class ZmHeapCacheT; 

public:
  static void init(
    ZuCSpan id, uint16_t partition, uint8_t vshift,
    const ZmHeapConfig &config);

#ifdef ZmHeap_DEBUG
  using TraceFn = ZmHeapCache::TraceFn;

  static void trace(ZuCSpan id, TraceFn allocFn, TraceFn freeFn);
#endif

private:
  static ZmHeapCache *cache(
    ZuCSpan id, uint8_t vshift,
    uint32_t size, uint16_t alignment, bool sharded);
};

// TLS pointer to the shared arena for this partition and heap specialization
template <
  typename ID_,
  unsigned Size_,
  unsigned Align_,
  bool Sharded_,
  unsigned VShift_ = 0>
class ZmHeapCacheT : public ZmObject {
  ZuDerive(TLS,
    (ZmSpecific<ZmHeapCacheT, ZmSpecificCleanup<ZmCleanup::Heap>>));

public:
  using ID = ID_;
  static constexpr unsigned Size = Size_;
  static constexpr unsigned Align = Align_;
  static constexpr bool Sharded = Sharded_;
  static constexpr unsigned VShift = VShift_;

  ZmHeapCacheT() :
    m_cache{ZmHeapMgr::cache(ID{}(), VShift, Size, Align, Sharded)} { }

  static ZmHeapCacheT *instance() { return TLS::instance(); }
  ZuInline ZmHeapCache *cache() const { return m_cache; }

  static void *alloc() {
    ZmHeapCacheT *this_ = instance();
    return this_->cache()->template alloc<Align>();
  }
  static void free(void *p) {
    ZmHeapCacheT *this_ = instance();
    this_->cache()->free(p);
  }

  static void warmup() {
    ZmHeapCacheT *this_ = instance();
    this_->cache()->warmup();
  }

private:
  ZmHeapCache	*m_cache;
};

// ZmHeapAllocSize evaluates to a size that is:
// - rounded up to sizeof(uintptr_t) if smaller, or
// - the smallest power of 2 greater than the passed size yet smaller
//   than the cache line size if smaller than that, or
// - the size rounded up to the nearest multiple of the cache line size
template <
  uint64_t Size_,
  bool Small = (Size_ <= sizeof(uintptr_t)),
  unsigned RShift = 0,
  bool Big = (Size_ > (Zm::CacheLineSize>>RShift))>
struct ZmHeapAllocSize;
template <uint64_t Size_, unsigned RShift, bool Big>
struct ZmHeapAllocSize<Size_, true, RShift, Big> : // smallest
  public ZuUnsigned<sizeof(uintptr_t)> { };
template <uint64_t Size_, unsigned RShift>
struct ZmHeapAllocSize<Size_, false, RShift, false> : // smaller
  public ZmHeapAllocSize<Size_, false, RShift + 1> { };
template <uint64_t Size_, unsigned RShift>
struct ZmHeapAllocSize<Size_, false, RShift, true> : // larger
  public ZuConstant<uint64_t, (Zm::CacheLineSize>>(RShift - 1))> { };
template <uint64_t Size_>
struct ZmHeapAllocSize<Size_, false, 0, true> : // larger than cache line size
  public ZuConstant<uint64_t,
    ((Size_ + Zm::CacheLineSize - 1) & ~(Zm::CacheLineSize - 1))> { };

using ZmNHeapDisable = ZuStringT<"">;
inline ZmNHeapDisable ZmNHeapID(...);	// default - ZmNHeap disabled
inline ZuFalse ZmNHeapSharded(...);	// default - not sharded

template <typename T>
using ZmNHeapID_ = decltype(ZmNHeapID(ZuDeclVal<T *>()));
template <typename T>
using ZmNHeapSharded_ = decltype(ZmNHeapSharded(ZuDeclVal<T *>()));

// ZmNHeap can be disabled at two levels:
// - ZmNHeap<..., false> suppresses declaration of `new` and `delete`
// - ADL lookup of ZmNHeapDisable ZmNHeapID(T *)
//   declares `new` and `delete` that forward to the global defaults

template <typename T, bool Enabled = true>
struct ZmNHeap {
  inline void *operator new(size_t);
  inline void *operator new(size_t, void *);
  inline void operator delete(void *) noexcept;
};

template <typename T>
struct ZmNHeap<T, false> { };

template <typename T, bool Enabled>
ZuInline void *ZmNHeap<T, Enabled>::operator new(size_t size) {
  using ID = ZmNHeapID_<T>;
  if constexpr (!ZuIs_<ID, ZmNHeapDisable>{}) {
    using Sharded = ZmNHeapSharded_<T>;
    using Cache = ZmHeapCacheT<
      ID, ZmHeapAllocSize<sizeof(T)>{}, alignof(T), Sharded{}>;
    return Cache::alloc();
  } else
    return ::operator new(size);
}
template <typename T, bool Enabled>
ZuInline void *ZmNHeap<T, Enabled>::operator new(size_t, void *p) {
  return p;
}
template <typename T, bool Enabled>
ZuInline void ZmNHeap<T, Enabled>::operator delete(void *p) noexcept {
  if (ZuUnlikely(!p)) return;
  using ID = ZmNHeapID_<T>;
  if constexpr (!ZuIs_<ID, ZmNHeapDisable>{}) {
    using Sharded = ZmNHeapSharded_<T>;
    using Cache = ZmHeapCacheT<
      ID, ZmHeapAllocSize<sizeof(T)>{}, alignof(T), Sharded{}>;
    Cache::free(p);
  } else
    return ::operator delete(p);
}

#include <zlib/ZmFn.hh>

#endif /* ZmHeap_HH */
