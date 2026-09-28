//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// block allocator with affinitized cache (free list) and statistics

#include <stdlib.h>

#include <zlib/ZmHeap.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmScratch.hh>
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

  ZmLHashKVDerive(Hash, uintptr_t, ZmHeapCache *, ZmLHashLocal<>);

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
friend void ZmHeapOnFail(ZmHeapFailFn);
friend bool ZmHeapFail();

  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

  using CfKey = ZuTuple<ZuID, unsigned, uint8_t>; // id, partition, vshift
  using IDSize = ZuTuple<ZuCSpan, unsigned>;
  using Key = ZmHeapCache::Key;

  // these containers use a null heap ID to prevent a circular dependency

  // primary key for heap configurations is {ID, partition}
  ZmRBTreeDerive(Configs, (ZuTuple<CfKey, ZmHeapConfig>),
    ZmRBTreeKeyVal<ZuTupleAxor<0>(), ZuTupleAxor<1>(),
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"">>>);
  // id2Cache is non-unique map used to find and configure heaps that were
  // constructed prior to configuration, and enable/disable tracing by apps
  ZmRBTreeDerive(ID2Cache, ZmHeapCache *,
    ZmRBTreeKey<ZmHeapCache::IDAxor,
      ZmRBTreeHeapID<"">>);
  // key2Cache is unique map from primary key to individual heap cache;
  // primary key for a heap is {ID, partition, size, sharded}
  ZmRBTreeDerive(Key2Cache, ZmHeapCache *,
    ZmRBTreeKey<ZmHeapCache::KeyAxor,
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"">>>);
  // lookups are only used for non-sharded heaps; primary key is {ID, size};
  // IDSize2Lookup maps direct from {ID, size, address} to individual heap
  // for free()
  ZmRBTreeDerive(IDSize2Lookup, (ZuTuple<IDSize, ZmHeapLookup>),
    ZmRBTreeKeyVal<ZuTupleAxor<0>(), ZuTupleAxor<1>(),
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"">>>);
  // id2Stats is used to find global heap stats
  ZmRBTreeKVDerive(ID2Stats, ZuID, ZmHeapGlobalStats,
    ZmRBTreeHeapID<"">);

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
      this // ZmDEREF uses this in debug mode
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

  void init(
    ZuCSpan id, uint16_t partition, uint8_t vshift,
    const ZmHeapConfig &config)
  {
    auto hwloc = ZmTopology::hwloc();
    Guard guard(m_lock);
    m_configs.del(ZuFwdTuple(id, partition, vshift));
    m_configs.add(ZuFwdTuple(id, partition, vshift), config);
    {
      auto i = m_id2Cache.citer<ZmRBTreeEqual>(id);
      while (ZmHeapCache *c = i.val()) {
	const auto &info = c->info();
	if (info.partition == partition && info.vshift == vshift) {
	  c->init(config, hwloc);
	  lookupAdd(c);
	}
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
    auto storage = ZmScratch(Ztc::HeapTelemetry, count);
    auto i = m_key2Cache.citer();
    while (ZmHeapCache *cache = i.val()) {
      if (match && !match(cache)) continue;
      auto data = new (storage.push()) Ztc::HeapTelemetry;
      cache->telemetry(*data);
    }
    guard.unlock();
    fn(storage.cspan());
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
    ZuCSpan id, uint8_t vshift,
    uint32_t size, uint16_t alignment, bool sharded)
  {
    uint16_t partition = ZmSelf()->partition();
    ZmHeapCache *cache = nullptr;
    auto hwloc = ZmTopology::hwloc();
    Guard guard(m_lock);
    if (cache = m_key2Cache.findVal(
	ZuFwdTuple(id, partition, size, alignment, sharded))) {
      return cache;
    }
    ID2Stats::Node *statsNode = nullptr;
    if (!(statsNode = m_stats.find(id)))
      statsNode = m_stats.add(id, ZmHeapGlobalStats{});
    ZmHeapGlobalStats *stats = &statsNode->val();
    if (Configs::NodeRef node =
	m_configs.find(ZuFwdTuple(id, partition, vshift))) {
      ZmHeapConfig config = node->val();
      cache = new ZmHeapCache(
	id, partition, vshift, config,
	size, alignment, sharded,
	stats, hwloc);
    } else {
      cache = new ZmHeapCache(
	id, partition, vshift, ZmHeapConfig{
	  .cacheSize = 0
	}, size, alignment, sharded,
	stats, hwloc);
    }
    ZmREF(cache);
    m_id2Cache.add(cache);
    m_key2Cache.add(cache);
    lookupAdd(cache);
    if (m_addFn) m_addFn(cache);
    return cache;
  }

  bool fail() {
    if (m_failFn && m_failFn()) return true;
    ::abort();
  }

  void failFn(ZmHeapFailFn fn) { m_failFn = fn; }

  ZmHeapFailFn		m_failFn = nullptr;
  ZmPLock		m_lock;
    Configs	 	  m_configs;
    ID2Cache		  m_id2Cache;
    Key2Cache		  m_key2Cache;
    IDSize2Lookup	  m_lookups;
    ID2Stats		  m_stats;
  Ztc::HeapMgr::AddFn	m_addFn;
  Ztc::HeapMgr::DelFn	m_delFn;
};

void ZmHeapOnFail(ZmHeapFailFn fn)
{
  ZmHeapMgr_::instance()->failFn(fn);
}

bool ZmHeapFail()
{
  return ZmHeapMgr_::instance()->fail();
}

void ZmHeapMgr::init(
  ZuCSpan id, uint16_t partition, uint8_t vshift, const ZmHeapConfig &config)
{
  ZmHeapMgr_::instance()->init(id, partition, vshift, config);
}

#ifdef ZmHeap_DEBUG
void ZmHeapMgr::trace(ZuCSpan id, TraceFn allocFn, TraceFn freeFn)
{
  ZmHeapMgr_::instance()->trace(id, allocFn, freeFn);
}
#endif

ZmHeapCache *ZmHeapMgr::cache(
  ZuCSpan id, uint8_t vshift, uint32_t size, uint16_t alignment, bool sharded)
{
  return ZmHeapMgr_::instance()->cache(
    id, vshift, size, alignment, sharded);
}

void *ZmHeapCache::operator new(size_t size) {
  void *ptr;
retry:
  if (ZuLikely(ptr = Zm::alignedAlloc<512>(size))) return ptr;
  if (ZmHeapFail()) goto retry;
  ZuUnreachable();
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
  ZuCSpan id, uint16_t partition, uint8_t vshift,
  const ZmHeapConfig &config,
  uint32_t size, uint16_t alignment, bool sharded,
  ZmHeapGlobalStats *globalStats,
  hwloc_topology_t hwloc)
  :
  m_info{id, config, size, partition, alignment, vshift, sharded},
  m_globalStats{globalStats}
{
  init_(hwloc);
}

ZmHeapCache::~ZmHeapCache()
{
  // stc::cerr << "~ZmHeapCache() 1 " << ZuBoxPtr(this) << '\n' << std::flush;
  final_();
  // stc::cerr << "~ZmHeapCache() 2 " << ZuBoxPtr(this) << '\n' << std::flush;
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

void ZmHeapCache::free(void *ptr)
{
  if (ZuUnlikely(!ptr)) return;
#ifdef ZmHeap_DEBUG
  {
    TraceFn fn;
    if (ZuUnlikely(fn = m_traceFreeFn)) (*fn)(m_info.id, m_info.size);
  }
#endif
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
      ++other->m_stats.crossFrees;
      other->free_(ptr);
      return;
    }
heapfree:
  ++m_stats.heapFrees;
  ++m_globalStats->heapFrees;
  Zm::alignedFree(ptr);
}

void ZmHeapCache::warmup()
{
  // no need to actually do anything here; heap configuration will
  // configure and initialize the cache
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

ZmHeapCache::TelKey ZmHeapCache::telKey() const
{
  return {
    m_info.id, m_info.partition,
    m_info.size, m_info.alignment, m_info.sharded
  };
}

void ZmHeapCache::telemetry(Ztc::HeapTelemetry &data) const
{
  data.id = m_info.id;
  data.cacheSize = m_info.config.cacheSize;
  data.cpuset = m_info.config.cpuset;
  data.cacheAllocs = m_stats.allocs.load_();
  data.heapAllocs = m_stats.heapAllocs.load_();
  data.cacheFrees = m_stats.frees.load_();
  data.crossFrees = m_stats.crossFrees.load_();
  data.heapFrees = m_stats.heapFrees.load_();
  data.globalHeapAllocs = m_globalStats->heapAllocs.load_();
  data.globalHeapFrees = m_globalStats->heapFrees.load_();
  data.globalHeapMax = m_globalStats->heapMax.load_();
  data.size = m_info.size;
  data.partition = m_info.partition;
  data.sharded = m_info.sharded;
  data.alignment = m_info.alignment;
  data.vshift = m_info.vshift;
}
