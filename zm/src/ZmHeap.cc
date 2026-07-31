//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// block allocator with affinitized cache (free list) and statistics

#include <stdlib.h>

#include <zlib/ZmHeap.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmAlloc.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmTopology.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmLHash.hh>

class ZmHeapMgr;
class ZmHeapCache;

class ZmHeapLookup {
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmGuard<Lock>;

public:
  static constexpr unsigned hashSize() { return 8; }

  ZuDerive(Hash, (ZmLHashKV<uintptr_t, ZmHeapCache *, ZmLHashLocal<>>));

public:
  ZmHeapLookup() : m_hash{ZmHashParams{hashSize()}} { }

  void add(ZmHeapCache *c) {
    auto begin = reinterpret_cast<uintptr_t>(c->begin());
    auto end = reinterpret_cast<uintptr_t>(c->end()) - 1;
    Guard guard(m_lock);
    if (ZuUnlikely(!m_shift))
      m_shift = (sizeof(end)<<3) - ZuIntrin::clz(end - begin);
    begin >>= m_shift;
    end >>= m_shift;
    m_hash.add(begin, c);
    if (end != begin) m_hash.add(end, c);
    c->lookup(this);
  }
  void del(ZmHeapCache *c) {
    auto begin = reinterpret_cast<uintptr_t>(c->begin());
    auto end = reinterpret_cast<uintptr_t>(c->end()) - 1;
    Guard guard(m_lock);
    begin >>= m_shift;
    end >>= m_shift;
    m_hash.del(begin, c);
    if (end != begin) m_hash.del(end, c);
    c->lookup(nullptr);
  }

  ZmHeapCache *find(ZmHeapCache *skip, void *p) const {
    ReadGuard guard(m_lock);
    if (ZuUnlikely(!m_shift)) return nullptr;
    uintptr_t key = reinterpret_cast<uintptr_t>(p)>>m_shift;
    auto i = m_hash.citer(key);
    while (ZmHeapCache *c = i.val())
      if (ZuLikely(c != skip && c->owned(p))) return c;
    return nullptr;
  }

private:
  mutable Lock		m_lock;
    unsigned		  m_shift = 0;
    Hash		  m_hash;
};

class ZmHeapMgr_ : public ZmObject {
friend ZmHeapMgr;
friend ZmHeapCache;
friend Ztc::HeapMgr;

  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

  using IDPart = ZuTuple<ZuID, unsigned>;
  using IDSize = ZuTuple<ZuCSpan, unsigned>;
  using Key = ZmHeapCache::Key;

  // these containers use a null heap ID to prevent a circular dependency

  // primary key for heap configurations is {ID, partition}
  ZuDerive(IDPart2Config,
    (ZmRBTreeKV<IDPart, ZmHeapConfig,
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"">>>));
  // id2Cache is non-unique map used to find and configure heaps that were
  // constructed prior to configuration, and enable/disable tracing by apps
  ZuDerive(ID2Cache,
    (ZmRBTree<ZmHeapCache *,
      ZmRBTreeKey<ZmHeapCache::IDAxor,
	ZmRBTreeHeapID<"">>>));
  // key2Cache is unique map from primary key to individual heap cache;
  // primary key for a heap is {ID, partition, size, sharded}
  ZuDerive(Key2Cache,
    (ZmRBTree<ZmHeapCache *,
      ZmRBTreeKey<ZmHeapCache::KeyAxor,
	ZmRBTreeUnique<true,
	  ZmRBTreeHeapID<"">>>>));
  // lookups are only used for non-sharded heaps; primary key is {ID, size};
  // IDSize2Lookup maps direct from {ID, size, address} to individual heap
  // for free()
  ZuDerive(IDSize2Lookup,
    (ZmRBTreeKV<IDSize, ZmHeapLookup,
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"">>>));

  using ReportFn = ZmHeapReportFn;

#ifdef ZmHeap_DEBUG
  using TraceFn = ZmHeapMgr::TraceFn;
#endif

public:
  ZmHeapMgr_() = default;
  ~ZmHeapMgr_() {
    m_addFn = {};
    m_delFn = {};
    m_key2Cache.clean();
    m_id2Cache.clean([
#ifdef ZmObject_DEBUG
      this
#endif
    ](auto node) {
      ZmDEREF(node->val());
    });
  }

private:
  static ZmHeapMgr_ *instance() {
    return
      ZmSingleton<ZmHeapMgr_,
	ZmSingletonCleanup<ZmCleanup::HeapMgr>>::instance();
  }

  void init(ZuCSpan id, unsigned partition, const ZmHeapConfig &config) {
    auto hwloc = ZmTopology::hwloc();
    Guard guard(m_lock);
    m_configs.del(ZuFwdTuple(id, partition));
    m_configs.add(ZuFwdTuple(id, partition), config);
    {
      auto i = m_id2Cache.citer<ZmRBTreeEqual>(id);
      while (ZmHeapCache *c = i.val())
	if (c->info().partition == partition) {
	  c->init(config, hwloc);
	  lookupAdd(c);
	}
    }
  }

  unsigned all(Ztc::HeapMgr::AllFn fn) {
    unsigned n = 0;
    ZmRef<ZmHeapCache> c;
    {
      Guard guard(m_lock);
      c = m_key2Cache.minimumVal();
    }
    while (c) {
      ++n;
      fn(c.ptr());
      {
	Guard guard(m_lock);
	c = m_key2Cache.citer<ZmRBTreeGreater>(
	    ZmHeapCache::KeyAxor(c)).val();
      }
    }
    return n;
  }

  void capture(
      Ztc::HeapMgr::MatchFn match, Ztc::HeapMgr::CaptureFn fn) {
    Guard guard(m_lock);
    unsigned count = m_key2Cache.count_();
    auto storage = ZmAlloc(Ztc::HeapTelemetry, count);
    unsigned length = 0;
    auto i = m_key2Cache.citer();
    while (ZmHeapCache *cache = i.val()) {
      if (match && !match(cache)) continue;
      auto data = new (&storage[length++]) Ztc::HeapTelemetry;
      cache->telemetry(*data);
    }
    guard.unlock();
    fn(ZuSpan<const Ztc::HeapTelemetry>{storage.ptr, length});
    for (unsigned i = 0; i < length; ++i)
      storage[i].~HeapTelemetry();
  }

  void watch(Ztc::HeapMgr::AddFn addFn, Ztc::HeapMgr::DelFn delFn) {
    Guard guard(m_lock);
    ZmAssert(!m_addFn && !m_delFn, return);
    m_addFn = ZuMv(addFn);
    m_delFn = ZuMv(delFn);
  }

  void unwatch() {
    Guard guard(m_lock);
    m_addFn = {};
    m_delFn = {};
  }

#if 0
  void all(ZuCSpan id, Ztc::HeapMgr::AllFn fn) {
    Key key{id, 0U, 0U, false};
    ZmRef<ZmHeapCache> c;
    for (;;) {
      {
	Guard guard(m_lock);
	c = m_key2Cache.citer<ZmRBTreeGreater>(key).val();
      }
      if (!c) return;
      if (id != c->info().id) return;
      key = ZmHeapCache::KeyAxor(c);
      fn(c);
    }
  }
#endif

  Lock &watchLock() { return m_lock; }

#ifdef ZmHeap_DEBUG
  void trace(ZuCSpan id, TraceFn allocFn, TraceFn freeFn) {
    auto i = m_id2Cache.citer<ZmRBTreeEqual>(id);
    while (ZmHeapCache *c = i.val()) {
      c->traceAllocFn(allocFn);
      c->traceFreeFn(freeFn);
    }
  }
#endif

  void lookupAdd(ZmHeapCache *c) {
    const auto &info = c->info();
    if (info.sharded || !info.config.cacheSize || c->lookup()) return;
    IDSize2Lookup::Node *node =
      m_lookups.find(ZuFwdTuple(info.id, info.size));
    if (!node) {
      node = new IDSize2Lookup::Node{};
      node->key() = ZuFwdTuple(info.id, info.size);
      m_lookups.addNode(node);
    }
    node->val().add(c);
  }

  ZmHeapCache *cache(
    ZuCSpan id, unsigned size, unsigned alignment, bool sharded,
    unsigned vshift, ReportFn reportFn)
  {
    unsigned partition = ZmSelf()->partition();
    ZmHeapCache *c = nullptr;
    auto hwloc = ZmTopology::hwloc();
    Guard guard(m_lock);
    if (c = m_key2Cache.findVal(
	ZuFwdTuple(id, partition, size, alignment, sharded))) {
      return c;
    }
    if (IDPart2Config::NodeRef node =
	m_configs.find(ZuFwdTuple(id, partition))) {
      ZmHeapConfig config = node->val();
      c = new ZmHeapCache(
	id, size, alignment, partition, sharded, vshift, config,
	reportFn, hwloc);
    } else {
      c = new ZmHeapCache(
	id, size, alignment, partition, sharded, vshift, ZmHeapConfig{
	  .cacheSize = 0
	}, reportFn, hwloc);
    }
    ZmREF(c);
    m_id2Cache.add(c);
    m_key2Cache.add(c);
    lookupAdd(c);
    if (m_addFn) m_addFn(c);
    return c;
  }

  ZmPLock		m_lock;
    IDPart2Config	  m_configs;
    ID2Cache		  m_id2Cache;
    Key2Cache		  m_key2Cache;
    IDSize2Lookup	  m_lookups;
  Ztc::HeapMgr::AddFn	m_addFn;
  Ztc::HeapMgr::DelFn	m_delFn;
};

void ZmHeapMgr::init(
  ZuCSpan id, unsigned partition, const ZmHeapConfig &config)
{
  ZmHeapMgr_::instance()->init(id, partition, config);
}

#ifdef ZmHeap_DEBUG
void ZmHeapMgr::trace(ZuCSpan id, TraceFn allocFn, TraceFn freeFn)
{
  ZmHeapMgr_::instance()->trace(id, allocFn, freeFn);
}
#endif

ZmHeapCache *ZmHeapMgr::cache(
  ZuCSpan id, unsigned size, unsigned alignment, bool sharded, unsigned shift,
  ReportFn reportFn)
{
  return ZmHeapMgr_::instance()->cache(
    id, size, alignment, sharded, shift, reportFn);
}

void *ZmHeapCache::operator new(size_t size) {
  void *ptr = Zm::alignedAlloc<512>(size);
  if (ZuUnlikely(!ptr)) throw std::bad_alloc{};
  return ptr;
}
void *ZmHeapCache::operator new(size_t, void *ptr)
{
  return ptr;
}
void ZmHeapCache::operator delete(void *ptr)
{
  Zm::alignedFree(ptr);
}

ZmHeapCache::ZmHeapCache(
  ZuCSpan id, unsigned size, unsigned alignment,
  unsigned partition, bool sharded, unsigned vshift,
  const ZmHeapConfig &config,
  ReportFn reportFn,
  hwloc_topology_t hwloc) :
  m_vshift{vshift},
  m_info{id, size, alignment, partition, sharded, config},
  m_reportFn{reportFn}
{
  init_(hwloc);
}

ZmHeapCache::~ZmHeapCache()
{
  // printf("~ZmHeapCache() 1 %p\n", this); fflush(stdout);
  final_();
  // printf("~ZmHeapCache() 2 %p\n", this); fflush(stdout);
}

void ZmHeapCache::init(const ZmHeapConfig &config, hwloc_topology_t hwloc)
{
  if (m_info.config.cacheSize) return; // resize is not supported
  m_info.config = config;
  init_(hwloc);
}

// init_() may be called once or twice at most
// - once during initial construction, often with cacheSize == 0
// - if construction preceded configuration, once again with cacheSize != 0
void ZmHeapCache::init_(hwloc_topology_t hwloc)
{
  ZmHeapConfig &config = m_info.config;
  if (!config.cacheSize) return;
  m_info.size = (m_info.size + m_info.alignment - 1) & ~(m_info.alignment - 1);
  config.cacheSize >>= m_vshift; // this only occurs once
  uint64_t len = config.cacheSize * m_info.size;
  void *begin;
  if (!config.cpuset)
    begin = hwloc_alloc(hwloc, len);
  else
    begin = hwloc_alloc_membind(
      hwloc, len, config.cpuset, HWLOC_MEMBIND_BIND, 0);
  if (!begin) { config.cacheSize = 0; return; }
  uintptr_t n = 0;
  for (auto p = reinterpret_cast<uintptr_t>(begin) + len;
      (p -= m_info.size) >= reinterpret_cast<uintptr_t>(begin); )
    *reinterpret_cast<uintptr_t *>(p) = n, n = p;
  m_begin = begin;
  m_end = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(begin) + len);
  m_head = reinterpret_cast<uintptr_t>(begin); // assignment causes release
}

void ZmHeapCache::final_()
{
  if (m_lookup)
    m_lookup->del(this);
  if (m_begin)
    hwloc_free(ZmTopology::hwloc(),
	m_begin, m_info.config.cacheSize * m_info.size);
}

void ZmHeapCache::free(ZmHeapStats &stats, void *ptr)
{
  if (ZuUnlikely(!ptr)) return;
#ifdef ZmHeap_DEBUG
  {
    TraceFn fn;
    if (ZuUnlikely(fn = m_traceFreeFn)) (*fn)(m_info.id, m_info.size);
  }
#endif
  ++stats.frees;
  // sharded - no contention, no need to check other partitions
  if (ZuLikely(m_info.sharded)) {
    if (ZuLikely(owned(ptr))) {
      free_sharded(ptr);
      return;
    }
    goto heapfree;
  }
  // check own cache first - optimize for malloc()/free() within same partition
  if (ZuLikely(owned(ptr))) {
    free_(ptr);
    return;
  }
  if (auto lookup = this->lookup())
    if (auto other = lookup->find(this, ptr)) {
      ++stats.crossFrees;
      other->free_(ptr);
      return;
    }
heapfree:
  Zm::alignedFree(ptr);
}

void ZmHeapCache::warmup()
{
  // no need to actually do anything here; heap configuration will
  // configure and initialize the cache
}

// report() iterates over the ZmHeapCacheT instances using
// ZmSpecific::all, compiling aggregate statistics from the
// thread-specific instance
void ZmHeapCache::report() const
{
  {
    HistReadGuard guard{m_histLock};
    m_stats = m_histStats;
  }
  m_reportFn(); // calls ZmHeapCacheT::report() { TLS::all(...) }
}

void ZmHeapCache::histStats(const ZmHeapStats &s) const
{
  HistGuard guard{m_histLock};
  m_histStats.heapAllocs += s.heapAllocs;
  m_histStats.cacheAllocs += s.cacheAllocs;
  m_histStats.frees += s.frees;
  m_histStats.crossFrees += s.crossFrees;
}

// --- telemetry

unsigned Ztc::HeapMgr::all(AllFn fn)
{
  return ZmHeapMgr_::instance()->all(ZuMv(fn));
}

ZmPLock &Ztc::HeapMgr::watchLock_()
{
  return ZmHeapMgr_::instance()->watchLock();
}

void Ztc::HeapMgr::capture(MatchFn match, CaptureFn fn)
{
  ZmHeapMgr_::instance()->capture(ZuMv(match), ZuMv(fn));
}

void Ztc::HeapMgr::watch(AddFn addFn, DelFn delFn)
{
  ZmHeapMgr_::instance()->watch(ZuMv(addFn), ZuMv(delFn));
}

void Ztc::HeapMgr::unwatch()
{
  ZmHeapMgr_::instance()->unwatch();
}

ZuTuple<ZuCSpan, uint32_t, uint8_t, uint16_t, uint8_t>
ZmHeapCache::telKey() const
{
  return {
    m_info.id, m_info.size, m_info.alignment,
    m_info.partition, m_info.sharded
  };
}

void ZmHeapCache::telemetry(Ztc::HeapTelemetry &data) const
{
  report();
  data.id = m_info.id;
  data.cacheSize = m_info.config.cacheSize;
  data.cpuset = m_info.config.cpuset;
  data.cacheAllocs = m_stats.cacheAllocs;
  data.heapAllocs = m_stats.heapAllocs;
  data.frees = m_stats.frees;
  data.crossFrees = m_stats.crossFrees;
  data.size = m_info.size;
  data.partition = m_info.partition;
  data.sharded = m_info.sharded;
  data.alignment = m_info.alignment;
}
