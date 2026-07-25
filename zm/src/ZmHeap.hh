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
// - TLS free list
// - efficient statistics and telemetry (Ztel)
// - globally configured
//   - supports profile-guided optimization of heap configuration

// Note: classes using ZmHeap benefit from empty base optimization
// - with MSVC, only the FIRST base class is optimized by default
// - to ensure ZmHeap EBO with multiple inheritance, always
//   place the ZmHeap first in the list of base classes

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

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmBitmap.hh>
#include <zlib/ZmSpecific.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmGuard.hh>

#include <zlib/ZtcHeap.hh>

#if defined(ZDEBUG) && !defined(ZmHeap_DEBUG)
#define ZmHeap_DEBUG
#endif

class ZmHeapMgr;
class ZmHeapMgr_;
class ZmHeapCache;
template <typename ID, unsigned Size, unsigned Algnment, bool Sharded>
class ZmHeapBase;
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
  unsigned	size;
  unsigned	alignment;
  unsigned	partition;
  bool		sharded;
  ZmHeapConfig	config;
};

struct ZmHeapStats {
  uint64_t	heapAllocs;
  uint64_t	cacheAllocs;
  uint64_t	frees;
  uint64_t	crossFrees;
};

class ZmHeapLookup;

typedef void (*ZmHeapReportFn)();

// cache (LIFO free list) of fixed-size blocks; one per CPU set / NUMA node
class ZmAPI ZmHeapCache final : public ZmObject, public Ztc::Heap {
friend ZmHeapMgr;
friend ZmHeapMgr_;
friend ZmHeapLookup;
template <typename, unsigned, unsigned, bool> friend class ZmHeapBase;
template <typename, unsigned, unsigned, bool, unsigned>
friend class ZmHeapCacheT;

  enum { CacheLineSize = Zm::CacheLineSize };

  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

  using ReportFn = ZmHeapReportFn;

  void *operator new(size_t s);
  void *operator new(size_t s, void *p);
public:
  void operator delete(void *p);

private:
  ZmHeapCache(
      ZuCSpan id, unsigned size, unsigned alignment,
      unsigned partition, bool sharded, unsigned vshift,
      const ZmHeapConfig &, ReportFn, hwloc_topology_t);

  void lookup(ZmHeapLookup *l) { m_lookup = l; }
  ZmHeapLookup *lookup() const { return m_lookup; }

public:
  ~ZmHeapCache();

  const ZmHeapInfo &info() const { return m_info; }
  void *begin() const { return m_begin; }
  void *end() const { return m_end; }
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

  ZuTuple<ZuID, uint32_t, uint8_t, uint16_t, uint8_t>
    telKey() const override;
  void telemetry(Ztc::HeapTelemetry &data) const override;

#ifdef ZmHeap_DEBUG
  typedef void (*TraceFn)(ZuCSpan, unsigned);
#endif

private:
  void init(const ZmHeapConfig &, hwloc_topology_t);
  void init_(hwloc_topology_t);
  void final_();

  template <unsigned Align>
  void *alloc(ZmHeapStats &stats) {
#ifdef ZmHeap_DEBUG
    {
      TraceFn fn;
      if (ZuUnlikely(fn = m_traceAllocFn)) (*fn)(m_info.id, m_info.size);
    }
#endif
    void *ptr;
    if (ZuLikely(ptr = alloc_())) {
      ++stats.cacheAllocs;
      return ptr;
    }
    ptr = Zm::alignedAlloc<Align>(m_info.size);
    if (ZuUnlikely(!ptr)) throw std::bad_alloc{};
    ++stats.heapAllocs;
    return ptr;
  }

  void free(ZmHeapStats &stats, void *p);

  // lock-free MPMC LIFO slist

  void *alloc_() {
    uintptr_t p;
  loop:
    p = m_head.load_();
    if (ZuUnlikely(!p)) return nullptr;
    if (ZuLikely(m_info.sharded)) { // sharded - no contention
      m_head.store_(*reinterpret_cast<uintptr_t *>(p));
      return reinterpret_cast<void *>(p);
    }
    if (ZuUnlikely(p & 1)) { ZmAtomic_acquire(); goto loop; }
    if (ZuUnlikely(m_head.cmpXch(p | 1, p) != p)) goto loop;
    m_head = reinterpret_cast<ZmAtomic<uintptr_t> *>(p)->load_();
    return reinterpret_cast<void *>(p);
  }
  void free_(void *p) {
    uintptr_t n;
  loop:
    n = m_head.load_();
    if (n & 1) { ZmAtomic_acquire(); goto loop; }
    reinterpret_cast<ZmAtomic<uintptr_t> *>(p)->store_(n);
    if (m_head.cmpXch(reinterpret_cast<uintptr_t>(p), n) != n) goto loop;
  }
  void free_sharded(void *p) { // sharded - no contention
    *reinterpret_cast<uintptr_t *>(p) = m_head.load_();
    m_head.store_(reinterpret_cast<uintptr_t>(p));
  }

  bool owned(void *p) const {
    return p >= m_begin && p < m_end;
  }

  void report() const;
  void report_(const ZmHeapStats &s) { // aggregate statistics from ZmHeapCacheT
    m_stats.heapAllocs += s.heapAllocs;
    m_stats.cacheAllocs += s.cacheAllocs;
    m_stats.frees += s.frees;
    m_stats.crossFrees += s.crossFrees;
  }

  void histStats(const ZmHeapStats &stats) const;

  // cache, end, lookup are guarded by ZmHeapMgr

  enum {
    Padding = CacheLineSize - sizeof(uintptr_t)
  };
  ZmAtomic<uintptr_t>	m_head;		// free list (contended atomic)
  char			m__pad[Padding];

  unsigned		m_vshift;
  ZmHeapInfo		m_info;
  ZmHeapLookup		*m_lookup = nullptr;
  ReportFn		m_reportFn;	// aggregates stats from TLS

  void			*m_begin = nullptr;	// bound memory region
  void			*m_end = nullptr;	// end of memory region

  using HistLock = ZmPLock;
  using HistGuard = ZmGuard<HistLock>;
  using HistReadGuard = ZmReadGuard<HistLock>;

  mutable HistLock	m_histLock;
    mutable ZmHeapStats	  m_histStats{};// stats from exited threads
  mutable ZmHeapStats	m_stats{};	// aggregated on demand

#ifdef ZmHeap_DEBUG
  void traceAllocFn(TraceFn fn) { m_traceAllocFn = fn; }
  void traceFreeFn(TraceFn fn) { m_traceFreeFn = fn; }

  TraceFn		m_traceAllocFn = nullptr;
  TraceFn		m_traceFreeFn = nullptr;
#endif
};

class ZmAPI ZmHeapMgr {
friend ZmHeapCache;
template <typename, unsigned, unsigned, bool, unsigned>
friend class ZmHeapCacheT; 

public:
  static void init(ZuCSpan id, unsigned partition, const ZmHeapConfig &config);

#ifdef ZmHeap_DEBUG
  using TraceFn = ZmHeapCache::TraceFn;

  static void trace(ZuCSpan id, TraceFn allocFn, TraceFn freeFn);
#endif

private:
  using ReportFn = ZmHeapReportFn;

  static ZmHeapCache *cache(
    ZuCSpan id, unsigned size, unsigned alignment, bool sharded,
    unsigned vshift, ReportFn);
};

// TLS heap cache, specific to ID+size; maintains TLS heap statistics
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
    m_cache{ZmHeapMgr::cache(ID{}(), Size, Align, Sharded, VShift, &report)},
    m_stats{} { }
  ~ZmHeapCacheT() {
    m_cache->histStats(m_stats);
  }

private:
  ZmHeapStats &stats() { return m_stats; }
public:
  const ZmHeapStats &stats() const { return m_stats; }

  // report() uses ZmSpecific::all to iterate over all threads and
  // collect/aggregate statistics for each TLS instance
  static void report();

  static ZmHeapCacheT *instance() { return TLS::instance(); }
  ZuInline ZmHeapCache *cache() const { return m_cache; }

  static void *alloc() {
    ZmHeapCacheT *this_ = instance();
    return this_->cache()->template alloc<Align>(this_->stats());
  }
  static void free(void *p) {
    ZmHeapCacheT *this_ = instance();
    this_->cache()->free(this_->stats(), p);
  }

  static void warmup() {
    ZmHeapCacheT *this_ = instance();
    this_->cache()->warmup();
  }

private:
  ZmHeapCache	*m_cache;
  ZmHeapStats	m_stats;
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

template <typename Cache> struct ZmHeap_Warmup { ZmHeap_Warmup(); };

template <typename ID_, unsigned Size_, unsigned Align_, bool Sharded_>
class ZmHeapBase {
public:
  using ID = ID_;
  enum { AllocSize = ZmHeapAllocSize<Size_>{} };
  enum { Align = Align_ };
  enum { Sharded = Sharded_ };

private:
  using Cache = ZmHeapCacheT<ID, AllocSize, Align, Sharded>;
  using Warmup = ZmHeap_Warmup<Cache>;

public:
  void *operator new(size_t) { (void)&m_warmup; return Cache::alloc(); }
  void *operator new(size_t, void *p) noexcept { return p; }
  void operator delete(void *p) noexcept {
    if (ZuUnlikely(!p)) return;
    Cache::free(p);
  }

private:
  static Warmup		m_warmup;
};

// mitigate cold start
template <typename Cache>
ZmHeap_Warmup<Cache>::ZmHeap_Warmup() { Cache::warmup(); }

template <typename ID, unsigned Size, unsigned Align, bool Sharded>
inline typename ZmHeapBase<ID, Size, Align, Sharded>::Warmup
ZmHeapBase<ID, Size, Align, Sharded>::m_warmup;

ZuFalse ZmHeap_Disabled_(...); // default

template <typename ID>
using ZmHeap_Disabled = decltype(ZmHeap_Disabled_(ZuDeclVal<ZuDecay<ID> *>()));

ZuTrue ZmHeap_Disabled_(ZuStringT<""> *);

template <
  typename ID,
  unsigned Size,
  unsigned Align,
  bool Sharded,
  bool Disabled = ZmHeap_Disabled<ID>{}>
struct ZmHeap__ { using T = ZmHeapBase<ID, Size, Align, Sharded>; };

template <
  typename ID,
  unsigned Size,
  unsigned Align,
  bool Sharded>
struct ZmHeap__<ID, Size, Align, Sharded, true> { using T = ZuVoid; };

template <typename ID, typename T, bool Sharded = false>
using ZmHeap_ = typename ZmHeap__<ID, sizeof(T), alignof(T), Sharded>::T;

template <ZuString ID, typename T, bool Sharded = false>
ZuDerive(ZmHeap, (ZmHeap_<ZuStringT<ID>, T, Sharded>));

#include <zlib/ZmFn.hh>

template <
  typename ID, unsigned Size, unsigned Align, bool Sharded, unsigned VShift>
inline void ZmHeapCacheT<ID, Size, Align, Sharded, VShift>::report()
{
  // aggregate heap cache statistics
  TLS::all([](ZmHeapCacheT *this_) {
    this_->cache()->report_(this_->stats());
  });
}

template <ZuString ID, typename T, bool Sharded>
auto ZmHeapID_(ZmHeap<ID, T, Sharded> *) -> ZuStringT<ID>;
ZuStringT<""> ZmHeapID_(...);
template <typename T>
using ZmHeapID = decltype(ZmHeapID_(ZuDeclVal<ZuDecay<T> *>()));

#endif /* ZmHeap_HH */
