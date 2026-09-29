//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#ifdef _WIN32
#include <process.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// The internal lookup fixture and hooks live only in this executable.
#include "../src/ZmHeap.cc"

using namespace ZuTestUtil;

ZmHeapTest::Hook ZmHeapTest::m_hook = nullptr;
const ZmHeapCache *ZmHeapTest::m_fail = nullptr;
unsigned ZmHeapTest::m_managers = 0;

void ZmHeapTest::hook(
    unsigned phase, const ZmHeapCache *cache, const void *ptr)
{
  if (phase == Created) ++m_managers;
  if (m_hook) m_hook(phase, cache, ptr);
}

bool ZmHeapTest::fail(const ZmHeapCache *cache)
{
  if (cache != m_fail) return false;
  m_fail = nullptr;
  return true;
}

void ZmHeapTest::publication()
{
  ZuTestScope(publication);
  using Cache = ZmHeapCacheT<ZuStringT<"ZmHeapTest.Publish">, 64, 8, false>;
  struct State {
    ZmSemaphore ready, allocGo, allocDone, freeGo, freeDone, finish;
    ZmHeapCache *source = nullptr;
    ZmHeapCache *receiver = nullptr;
    void *ptr = nullptr;
    unsigned phase = 0;
    unsigned arenaMask = 0;
  } state;
  static State *active;
  active = &state;
  auto partition = ZmSelf()->partition();
  ZmHeapMgr::init("ZmHeapTest.Publish", partition + 1, 0, {4});
  ZmThread producer{[&state] {
    state.source = Cache::instance()->cache();
    state.ready.post();
    void *fallback = nullptr, *held = nullptr;
    for (unsigned phase = Prepared; phase <= Published; ++phase) {
      state.allocGo.wait();
      if (phase == Prepared) fallback = Cache::alloc();
      if (phase == Published) held = Cache::alloc();
      state.ptr = Cache::alloc();
      if (state.source->owned(state.ptr)) state.arenaMask |= 1U<<phase;
      state.allocDone.post();
    }
    state.finish.wait();
    Cache::free(fallback);
    Cache::free(held);
  }, ZmThreadParams{}.partition(partition)};
  ZmThread consumer{[&state] {
    state.receiver = Cache::instance()->cache();
    state.ready.post();
    for (unsigned phase = Prepared; phase <= Published; ++phase) {
      state.freeGo.wait();
      Cache::free(state.ptr);
      state.freeDone.post();
    }
  }, ZmThreadParams{}.partition(partition + 1)};
  state.ready.wait();
  state.ready.wait();
  m_hook = [](unsigned phase, const ZmHeapCache *cache, const void *) {
    if (cache != active->source || phase > Published) return;
    active->phase = phase;
    active->allocGo.post();
    active->allocDone.wait();
    active->freeGo.post();
    active->freeDone.wait();
  };
  ZmHeapMgr::init("ZmHeapTest.Publish", partition, 0, {4});
  state.finish.post();
  producer.join();
  consumer.join();
  m_hook = nullptr;
  ZuCheck(state.phase == Published);
  ZuCheck(state.arenaMask == (1U<<Published));
  ZuCheck(state.source->stats().allocs.load_() == 2);
  ZuCheck(state.source->stats().frees.load_() == 2);
  ZuCheck(state.source->stats().crossFrees.load_() == 1);
  ZuCheck(state.source->stats().heapAllocs.load_() == 3);
  ZuCheck(state.source->stats().heapFrees.load_() == 1);
  ZuCheck(state.receiver->stats().heapFrees.load_() == 2);
}

ZmHeapCache *ZmHeapTest::arena(
    void *begin, unsigned size, ZmHeapGlobalStats &stats)
{
  auto cache = new ZmHeapCache{
    "ZmHeapTest.Range", 0, 0, {0}, 64, 8, false,
    &stats, ZmTopology::hwloc()};
  cache->m_begin = begin;
  cache->m_end = static_cast<uint8_t *>(begin) + size;
  return cache;
}

void ZmHeapTest::dispose(ZmHeapCache *cache)
{
  if (auto lookup = cache->lookup()) lookup->del(cache);
  // Synthetic ranges belong to the fixture's scratch storage.
  cache->m_begin = nullptr;
  cache->m_end = nullptr;
  delete cache;
}

void ZmHeapTest::bounds()
{
  ZuTestScope(bounds);
  unsigned size = 64;
  auto storage = ZmScratch(uint8_t, size + 2);
  ZmHeapGlobalStats stats;
  auto cache = arena(storage.data() + 1, size, stats);
  auto begin = cache->begin();
  auto end = cache->end();
  cache->m_begin = cache->m_end = nullptr;
  struct State {
    ZmSemaphore read, resume;
    const ZmHeapCache *cache;
    const void *snapshot = nullptr;
    bool owned = true;
  } state{{}, {}, cache};
  static State *active;
  active = &state;
  m_hook = [](unsigned phase, const ZmHeapCache *cache, const void *ptr) {
    if (phase != Owned || cache != active->cache) return;
    active->snapshot = ptr;
    active->read.post();
    active->resume.wait();
  };
  ZmThread reader{[&state, cache, ptr = storage.data()] {
    state.owned = cache->owned(ptr);
  }};
  state.read.wait();
  cache->m_begin = begin;
  cache->m_end = end;
  state.resume.post();
  reader.join();
  m_hook = nullptr;
  ZuCheck(!state.snapshot);
  ZuCheck(!state.owned);
  ZuCheck(!cache->owned(storage.data()));
  ZuCheck(cache->owned(begin));
  ZuCheck(cache->owned(static_cast<uint8_t *>(end) - 1));
  ZuCheck(!cache->owned(end));
  dispose(cache);
}

template <bool Sharded>
void ZmHeapTest::activation()
{
  ZuTestScope(activation);
  using ID = ZuIf<Sharded,
    ZuStringT<"ZmHeapTest.Sharded">, ZuStringT<"ZmHeapTest.Shared">>;
  using Cache = ZmHeapCacheT<ID, 64, 8, Sharded>;
  auto cache = Cache::instance()->cache();
  auto partition = ZmSelf()->partition();
  ZuCheck(!cache->begin());
  ZuTestCall(zero, cache, partition, uint8_t(0));
  ZuCheck(!cache->begin() && !cache->m_head.load_());
  auto fallback = Cache::alloc();
  ZmHeapMgr::init(ID{}(), partition, 0, {2});
  auto begin = cache->begin();
  ZuCheck(begin && cache->info().config.cacheSize == 2);
  ZuCheck(!cache->lookup());
  auto first = Cache::alloc();
  auto second = Cache::alloc();
  ZuCheck(first != second && cache->owned(first) && cache->owned(second));
  ZuCheck(!cache->m_head.load_());
  ZuTestCall(zero, cache, partition, uint8_t(0));
  auto exhausted = Cache::alloc();
  ZuCheck(!cache->owned(exhausted));
  ZuCheck(cache->begin() == begin && cache->info().config.cacheSize == 2);
  struct State { const ZmHeapCache *cache; unsigned finds = 0; } state{cache};
  static State *active;
  active = &state;
  m_hook = [](unsigned phase, const ZmHeapCache *c, const void *) {
    if (phase == Find && c == active->cache) ++active->finds;
  };
  Cache::free(fallback);
  Cache::free(exhausted);
  Cache::free(first);
  Cache::free(second);
  m_hook = nullptr;
  ZuCheck(!state.finds);
  ZuCheck(cache->stats().heapFrees.load_() == 2);
  ZuCheck(cache->stats().frees.load_() == 2);
  using Other = ZmHeapCacheT<ID, 128, 8, Sharded>;
  auto other = Other::instance()->cache();
  auto ptr = Other::alloc();
  ZuCheck(other->info().config.cacheSize == 2 && other->owned(ptr));
  Other::free(ptr);
  if constexpr (!Sharded) {
    auto manager = ZmHeapMgr_::instance();
    auto lookup = &manager->m_lookups.find(ZuFwdTuple(cache->info().id, 64U))->val();
    ZuCheck(covers(*lookup, cache) && !cache->lookup());
  }
}

unsigned ZmHeapTest::entries(ZmHeapLookup &lookup, ZmHeapCache *cache)
{
  unsigned count = 0;
  auto i = lookup.m_hash.citer();
  while (auto entry = i()) if (entry->p<1>() == cache) ++count;
  return count;
}

bool ZmHeapTest::covers(ZmHeapLookup &lookup, ZmHeapCache *cache)
{
  auto begin = static_cast<uint8_t *>(cache->begin());
  auto end = static_cast<uint8_t *>(cache->end());
  for (auto ptr = begin; ptr < end; ++ptr)
    if (lookup.find(nullptr, ptr) != cache) return false;
  auto first = reinterpret_cast<uintptr_t>(begin)>>lookup.m_shift;
  auto last = reinterpret_cast<uintptr_t>(end - 1)>>lookup.m_shift;
  return entries(lookup, cache) == last - first + 1 &&
    lookup.find(nullptr, begin - 1) != cache &&
    lookup.find(nullptr, end) != cache &&
    !lookup.find(cache, begin);
}

void ZmHeapTest::ranges()
{
  ZuTestScope(ranges);
  unsigned largeSize = 4096;
  // Alignment allowance, a large arena, and separated smaller fixture ranges.
  auto storage = ZmScratch(uint8_t, largeSize * 4);
  auto base = (reinterpret_cast<uintptr_t>(storage.data()) + largeSize - 1) &
    ~uintptr_t(largeSize - 1);
  ZmHeapGlobalStats stats;
  for (unsigned order = 0; order < 2; ++order) {
    ZmHeapLookup lookup;
    auto large = arena(reinterpret_cast<void *>(base + 8), largeSize, stats);
    auto small = arena(reinterpret_cast<void *>(base + largeSize * 2 + 8), 192, stats);
    auto tiny = arena(reinterpret_cast<void *>(base + largeSize * 2 + 264), 64, stats);
    auto equal = arena(reinterpret_cast<void *>(base + largeSize * 2 + 344), 64, stats);
    lookup.add(order ? small : large);
    lookup.add(order ? large : small);
    ZuCheck(lookup.m_shift == 8);
    ZuCheck(covers(lookup, small) && covers(lookup, large));
    lookup.add(tiny);
    ZuCheck(lookup.m_shift == 6);
    lookup.add(equal);
    ZuCheck(covers(lookup, small) && covers(lookup, large));
    ZuCheck(covers(lookup, tiny) && covers(lookup, equal));
    lookup.del(large);
    ZuCheck(!entries(lookup, large));
    ZuCheck(!lookup.find(nullptr, static_cast<uint8_t *>(large->begin()) + 576));
    ZuCheck(covers(lookup, small) && covers(lookup, tiny) && covers(lookup, equal));
    lookup.del(tiny);
    ZuCheck(!entries(lookup, tiny) && covers(lookup, equal));
    lookup.del(small);
    lookup.del(equal);
    ZuCheck(!lookup.m_hash.count_());
    lookup.add(large);
    ZuCheck(covers(lookup, large));
    lookup.del(large);
    dispose(large);
    dispose(small);
    dispose(tiny);
    dispose(equal);
    ZuCheck(!lookup.m_hash.count_());
  }
}

void ZmHeapTest::rebuild()
{
  ZuTestScope(rebuild);
  unsigned largeSize = 4096;
  auto storage = ZmScratch(uint8_t, largeSize * 2);
  ZmHeapGlobalStats stats;
  auto large = arena(storage.data(), largeSize, stats);
  auto small = arena(storage.data() + largeSize, 64, stats);
  ZmHeapLookup lookup;
  lookup.add(large);
  struct State {
    ZmSemaphore rebuilding, resume, finding;
    const ZmHeapLookup *lookup;
    ZmHeapCache *found = nullptr;
    ZmAtomic<unsigned> completed{0};
  } state{{}, {}, {}, &lookup};
  static State *active;
  active = &state;
  m_hook = [](unsigned phase, const ZmHeapCache *, const void *ptr) {
    if (ptr != active->lookup) return;
    switch (phase) {
      case Rebuild:
	active->rebuilding.post();
	active->resume.wait();
	break;
      case Find:
	active->finding.post();
	break;
    }
  };
  ZmThread updater{[&lookup, small] { lookup.add(small); }};
  state.rebuilding.wait();
  ZmThread reader{[&lookup, &state, large] {
    state.found = lookup.find(nullptr, static_cast<uint8_t *>(large->begin()) + 576);
    state.completed = 1;
  }};
  state.finding.wait();
  ZuCheck(!state.completed.load_());
  state.resume.post();
  updater.join();
  reader.join();
  m_hook = nullptr;
  ZuCheck(state.found == large);
  // These adjacent ranges share a boundary, so check their interiors directly.
  ZuCheck(lookup.find(nullptr, large->begin()) == large);
  ZuCheck(lookup.find(nullptr, small->begin()) == small);
  lookup.del(large);
  lookup.del(small);
  dispose(large);
  dispose(small);
}

template <typename Cache>
struct HeapWorker {
  ZmSemaphore go, done;
  ZmHeapCache *cache = nullptr;
  void *ptr = nullptr;
  unsigned command = 0;
  ZmThread thread;

  enum { Alloc, Free, Stop };

  HeapWorker(unsigned partition) : thread{[this] {
    cache = Cache::instance()->cache();
    done.post();
    for (;;) {
      go.wait();
      switch (command) {
	case Alloc: ptr = Cache::alloc(); break;
	case Free: Cache::free(ptr); ptr = nullptr; break;
	case Stop: return;
      }
      done.post();
    }
  }, ZmThreadParams{}.partition(partition)} { done.wait(); }
  ~HeapWorker() {
    command = Stop;
    go.post();
    thread.join();
  }
  void step(unsigned value, void *p = nullptr) {
    command = value;
    ptr = p;
    go.post();
    done.wait();
  }
};

void ZmHeapTest::receivers()
{
  ZuTestScope(receivers);
  using ID = ZuStringT<"ZmHeapTest.Receivers">;
  using Cache = ZmHeapCacheT<ID, 64, 8, false>;
  using Worker = HeapWorker<Cache>;
  auto partition = ZmSelf()->partition();
  auto source = Cache::instance()->cache();
  Worker first{unsigned(partition + 1)}, second{unsigned(partition + 2)};
  first.step(Worker::Alloc);
  auto fallback = first.ptr;
  ZmHeapMgr::init(ID{}(), partition, 0, {4});
  ZuCheck(!source->lookup() && !first.cache->lookup() && !second.cache->lookup());
  auto manager = ZmHeapMgr_::instance();
  auto lookup = &manager->m_lookups.find(ZuFwdTuple(source->info().id, 64U))->val();
  ZuCheck(covers(*lookup, source));
  ZuTestCall(zero, source, uint16_t(partition + 1), uint8_t(0));
  m_fail = first.cache;
  ZmHeapMgr::init(ID{}(), partition + 1, 0, {2});
  ZuCheck(!m_fail && !first.cache->begin());
  ZuCheck(first.cache->lookup() == lookup && source->lookup() == lookup);
  // Cache creation alone does not count, but eligible default receivers attach.
  ZuCheck(second.cache->lookup() == lookup);
  ZmHeapMgr::init(ID{}(), partition + 2, 0, {2});
  auto held = Cache::alloc();
  first.step(Worker::Free, Cache::alloc());
  second.step(Worker::Free, Cache::alloc());
  first.step(Worker::Free, fallback);
  ZuCheck(first.cache->stats().heapFrees.load_() == 1);
  ZuCheck(source->stats().crossFrees.load_() == 2);
  second.step(Worker::Alloc);
  auto remoteHeld = second.ptr;
  second.step(Worker::Alloc);
  ZuCheck(second.cache->owned(second.ptr));
  Cache::free(second.ptr);
  ZuCheck(second.cache->stats().crossFrees.load_() == 1);
  first.step(Worker::Free, remoteHeld);
  Cache::free(held);
  unsigned capacity = 64;
  ZmHeapMgr::init(ID{}(), partition + 3, 0, {capacity});
  Worker large{unsigned(partition + 3)};
  auto blocks = ZmScratch(void *, capacity);
  for (unsigned i = 0; i < capacity; ++i) {
    large.step(Worker::Alloc);
    blocks.push(large.ptr);
  }
  for (auto ptr : blocks) Cache::free(ptr);
  ZuCheck(large.cache->stats().crossFrees.load_() == capacity);
  ZmHeapMgr::init(ID{}(), partition + 4, 0, {1});
  Worker tiny{unsigned(partition + 4)};
  ZuCheck(tiny.cache->lookup() == lookup);
  ZuCheck(covers(*lookup, large.cache) && covers(*lookup, tiny.cache));
  ZuCheck(covers(*lookup, source) && covers(*lookup, second.cache));
  Worker later{unsigned(partition + 5)};
  ZuCheck(!later.cache->begin() && later.cache->lookup() == lookup);
  m_fail = later.cache;
  ZmHeapMgr::init(ID{}(), partition + 5, 0, {1});
  ZuCheck(!m_fail && !later.cache->begin() && later.cache->lookup() == lookup);
  auto block = Cache::alloc();
  ZuCheck(source->owned(block));
  later.step(Worker::Free, block);
  ZuCheck(source->stats().crossFrees.load_() == 3);
  ZmHeapGlobalStats stats;
  auto empty = new ZmHeapCache{ID{}(), uint16_t(partition), 0, {0}, 64, 8, false,
    &stats, ZmTopology::hwloc()};
  empty->lookup(lookup);
  auto count = lookup->m_hash.count_();
  delete empty;
  ZuCheck(lookup->m_hash.count_() == count);
}

void ZmHeapTest::zero(ZmHeapCache *cache, uint16_t partition, uint8_t vshift)
{
  ZuTestScope(zero);
  auto manager = ZmHeapMgr_::instance();
  auto id = cache->info().id;
  auto rows = manager->m_configs.count_();
  auto stats = manager->m_stats.count_();
  auto indexes = manager->m_lookups.count_();
  auto count = manager->partCount_(id, vshift);
  auto begin = cache->begin(), end = cache->end();
  auto head = cache->m_head.load_();
  auto lookup = cache->lookup();
  auto capacity = cache->info().config.cacheSize;
  ZmHeapMgr::init(id, partition, vshift, {0});
  ZuCheck(manager->m_configs.count_() == rows && manager->m_stats.count_() == stats);
  ZuCheck(manager->m_lookups.count_() == indexes && manager->partCount_(id, vshift) == count);
  ZuCheck(cache->begin() == begin && cache->end() == end && cache->m_head.load_() == head);
  ZuCheck(cache->lookup() == lookup && cache->info().config.cacheSize == capacity);
}

void ZmHeapTest::zeroFirst()
{
  ZuTestScope(zeroFirst);
  ZuCheck(!m_managers);
  ZmHeapMgr::init("ZmHeapTest.Zero", 3, 1, {0});
  ZuCheck(!m_managers);
}

int ZmHeapTest::duplicate(bool fail)
{
  constexpr auto id = "ZmHeapTest.Duplicate";
  auto manager = ZmHeapMgr_::instance();
  auto cache = manager->cache(id, 1, 64, 8, false);
  if (fail) m_fail = cache;
  auto partition = ZmSelf()->partition();
  ZmHeapMgr::init(id, partition, 1, {2});
  if (fail && (m_fail || cache->begin() || cache->info().config.cacheSize))
    return 2;
  auto begin = cache->begin();
  auto head = cache->m_head.load_();
  auto count = manager->m_configs.count_();
  auto lookup = cache->lookup();
  auto indexes = manager->m_lookups.count_();
  auto index = manager->m_lookups.find(ZuFwdTuple(cache->info().id, 64U));
  auto entries = index ? index->val().m_hash.count_() : 0;
  ZmHeapMgr::init(id, partition, 1, {9});
  // Reached only in release: duplicate rejection must preserve stored config,
  // including a positive row whose arena allocation failed.
  auto node = manager->m_configs.find(ZuFwdTuple(id, uint8_t(1), partition));
  return node && node->val().cacheSize == 2 &&
    manager->m_configs.count_() == count && manager->partCount_(id, 1) == 1 &&
    cache->begin() == begin && cache->m_head.load_() == head &&
    cache->lookup() == lookup && cache->info().config.cacheSize == (fail ? 0 : 2) &&
    manager->m_lookups.count_() == indexes &&
    (!index || (index->val().m_hash.count_() == entries && covers(index->val(), cache)))
    ? 0 : 2;
}

void ZmHeapTest::duplicates(const char *exe)
{
  ZuTestScope(duplicates);
  for (auto mode : {"--duplicate", "--duplicate-fail"}) {
#ifdef _WIN32
    const char *args[] = {exe, mode, nullptr};
    auto status = _spawnv(_P_WAIT, exe, args);
#ifdef ZDEBUG
    ZuCheck(status == 3);
#else
    ZuCheck(!status);
#endif
#else
    auto pid = fork();
    if (!pid) {
      execl(exe, exe, mode, nullptr);
      _exit(127);
    }
    if (pid < 0) { ZuCheck(false); continue; }
    int status = 0;
    pid_t waited;
    do waited = waitpid(pid, &status, 0); while (waited < 0 && errno == EINTR);
    ZuCheck(pid > 0 && waited == pid);
#ifdef ZDEBUG
    ZuCheck(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
    ZuCheck(WIFEXITED(status) && !WEXITSTATUS(status));
#endif
#endif
  }
}

void ZmHeapTest::groups()
{
  ZuTestScope(groups);
  for (unsigned reverse = 0; reverse < 2; ++reverse) {
    ZmHeapMgr_ manager;
    constexpr auto id = "ZmHeapTest.Groups";
    auto local = manager.cache(id, 0, 64, 8, false);
    auto aligned = manager.cache(id, 1, 64, 16, false);
    auto rounded = manager.cache(id, 1, 48, 64, false);
    auto large = manager.cache(id, 0, 128, 8, false);
    auto sharded = manager.cache(id, 0, 64, 8, true);
    auto other = manager.cache("ZmHeapTest.Groups.Other", 0, 64, 8, false);
    ZmHeapCache *remote = nullptr;
    ZmThread worker{[&] { remote = manager.cache(id, 1, 64, 16, false); },
      ZmThreadParams{}.partition(3)};
    worker.join();
    if (reverse) {
      manager.init(id, 3, 1, {3});
      manager.init(id, 0, 0, {8});
    } else {
      manager.init(id, 0, 0, {8});
      manager.init(id, 3, 1, {3});
    }
    ZuCheck(manager.partCount_(id, 0) == 1 && manager.partCount_(id, 1) == 1);
    ZuCheck(remote->info().config.cacheSize == 3 && remote->begin());
    ZuCheck(!local->lookup() && !remote->lookup() && !aligned->lookup());
    ZuCheck(rounded->info().size == 64 && !rounded->lookup());
    auto lookup = &manager.m_lookups.find(ZuFwdTuple(id, 64U))->val();
    auto largeLookup = &manager.m_lookups.find(ZuFwdTuple(id, 128U))->val();
    ZuCheck(covers(*lookup, local) && covers(*lookup, remote));
    ZuCheck(covers(*largeLookup, large));
    // Same full cache key resolves to its original cache, irrespective of shift.
    ZuCheck(manager.cache(id, 1, 64, 8, false) == local);
    manager.init(id, 0, 1, {4});
    ZuCheck(manager.partCount_(id, 0) == 1 && manager.partCount_(id, 1) == 2);
    ZuCheck(aligned->lookup() == lookup && remote->lookup() == lookup);
    ZuCheck(rounded->lookup() == lookup && covers(*lookup, rounded));
    ZuCheck(!local->lookup() && !large->lookup() && !sharded->lookup());
    manager.init("ZmHeapTest.Groups.Other", 0, 0, {2});
    manager.init("ZmHeapTest.Groups.Other", 1, 0, {2});
    ZuCheck(other->lookup() && !local->lookup());
    // Snapshot association before registration: no old ranges are reinserted.
    struct State {
      ZmHeapCache *local, *large, *aligned, *sharded;
      ZmHeapLookup *lookup, *largeLookup;
      unsigned entries, largeEntries, associations = 0;
      bool ok = false;
    } state{local, large, aligned, sharded, lookup, largeLookup,
      lookup->m_hash.count_(), largeLookup->m_hash.count_()};
    static State *active;
    active = &state;
    m_hook = [](unsigned phase, const ZmHeapCache *, const void *) {
      if (phase != Associated) return;
      auto &s = *active;
      ++s.associations;
      s.ok = s.local->lookup() == s.lookup && s.large->lookup() == s.largeLookup &&
	s.aligned->lookup() == s.lookup && !s.sharded->lookup() &&
	s.lookup->m_hash.count_() == s.entries &&
	s.largeLookup->m_hash.count_() == s.largeEntries && covers(*s.largeLookup, s.large);
    };
    manager.init(id, 1, 0, {2});
    m_hook = nullptr;
    ZuCheck(state.associations == 1 && state.ok);
    ZuCheck(manager.partCount_(id, 0) == 2 && manager.partCount_(id, 1) == 2);
    // Late construction uses the reordered full key and existing shared index.
    ZmHeapCache *later = nullptr;
    ZmThread laterWorker{[&] { later = manager.cache(id, 1, 128, 8, false); },
      ZmThreadParams{}.partition(3)};
    laterWorker.join();
    ZuCheck(later->info().config.cacheSize == 3 && later->lookup() == largeLookup);
    ZuCheck(covers(*largeLookup, large) && covers(*largeLookup, later));
    ZuCheck(covers(*lookup, local) && covers(*lookup, aligned) && covers(*lookup, remote));
    manager.init(id, 2, 0, {1});
    ZuCheck(manager.partCount_(id, 0) == 3 && manager.partCount_(id, 1) == 2);
  }
  // Both rows before caches; a failed default cache gets associated when the
  // first successful arena creates the index, including another eligible shift.
  ZmHeapMgr_ manager;
  constexpr auto id = "ZmHeapTest.Deferred";
  manager.init(id, 0, 0, {2});
  manager.init(id, 1, 0, {2});
  auto receiver = manager.cache(id, 0, 64, 8, false);
  ZuCheck(receiver->lookup() && receiver->m_head.load_());
  ZmHeapCache *later = nullptr;
  ZmThread worker{[&] { later = manager.cache(id, 0, 64, 8, false); },
    ZmThreadParams{}.partition(1)};
  worker.join();
  ZuCheck(later->lookup() == receiver->lookup() && later->m_head.load_());
  constexpr auto failedID = "ZmHeapTest.DeferredFail";
  auto failed = manager.cache(failedID, 1, 64, 16, false);
  m_fail = failed;
  manager.init(failedID, 0, 1, {2});
  manager.init(failedID, 1, 1, {2});
  ZuCheck(!failed->begin() && !failed->lookup());
  manager.init(failedID, 0, 0, {2});
  auto singleton = manager.cache(failedID, 0, 64, 8, false);
  ZuCheck(!singleton->lookup() && failed->lookup());
  auto block = singleton->alloc<8>();
  failed->free(block);
  ZuCheck(singleton->stats().crossFrees.load_() == 1);
}

void ZmHeapTest::transition(bool exhausted)
{
  ZuTestScope(transition);
  using Partial = ZmHeapCacheT<ZuStringT<"ZmHeapTest.Partial">, 64, 8, false>;
  using Full = ZmHeapCacheT<ZuStringT<"ZmHeapTest.Full">, 64, 8, false>;
  auto id = exhausted ? "ZmHeapTest.Full" : "ZmHeapTest.Partial";
  auto source = exhausted ? Full::instance()->cache() : Partial::instance()->cache();
  auto partition = ZmSelf()->partition();
  ZmHeapMgr::init(id, partition, 0, {8});
  auto manager = ZmHeapMgr_::instance();
  auto lookup = &manager->m_lookups.find(ZuFwdTuple(id, 64U))->val();
  auto heldCount = exhausted ? 8U : 3U;
  auto held = ZmScratch(void *, heldCount);
  for (unsigned i = 0; i < heldCount; ++i) held.push(source->alloc<8>());
  struct State {
    ZmHeapCache *source, *receiver = nullptr;
    ZmHeapLookup *lookup;
    ZmSemaphore ready, go, done;
    unsigned entries, shift, associated = 0, registered = 0;
    uintptr_t head;
    bool associationOK = false, registrationOK = false;
    void *remote = nullptr;
  } state{source, nullptr, lookup, {}, {}, {}, lookup->m_hash.count_(), lookup->m_shift,
    0, 0, source->m_head.load_()};
  static State *active;
  active = &state;
  ZmThread worker{[&] {
    state.receiver = exhausted ? Full::instance()->cache() : Partial::instance()->cache();
    state.ready.post();
    for (unsigned i = 0; i < 3; ++i) {
      state.go.wait();
      state.remote = state.receiver->alloc<8>();
      if (i < 2) {
	state.receiver->free(state.remote);
	state.remote = nullptr;
      }
      state.done.post();
    }
  }, ZmThreadParams{}.partition(partition + 1)};
  state.ready.wait();
  m_hook = [](unsigned phase, const ZmHeapCache *cache, const void *) {
    auto &s = *active;
    if (phase == Associated) {
      ++s.associated;
      s.associationOK = s.source->lookup() == s.lookup &&
	s.receiver->lookup() == s.lookup && s.lookup->m_hash.count_() == s.entries &&
	s.lookup->m_shift == s.shift && s.source->m_head.load_() == s.head &&
	covers(*s.lookup, s.source);
    }
    if (phase == Associated || (phase == Registered && cache == s.receiver)) {
      s.go.post();
      s.done.wait();
    }
    if (phase == Registered && cache == s.receiver) {
      ++s.registered;
      s.registrationOK = !s.receiver->m_head.load_() && s.lookup->m_shift < s.shift &&
	covers(*s.lookup, s.source) && covers(*s.lookup, s.receiver) &&
	s.source->lookup() == s.lookup && s.receiver->lookup() == s.lookup;
    }
  };
  ZmHeapMgr::init(id, partition + 1, 0, {1});
  m_hook = nullptr;
  ZuCheck(state.associated == 1 && state.associationOK);
  ZuCheck(state.registered == 1 && state.registrationOK);
  state.go.post();
  state.done.wait();
  worker.join();
  ZuCheck(state.receiver->owned(state.remote));
  source->free(state.remote);
  auto available = exhausted ? 0U : 5U;
  auto remaining = ZmScratch(void *, available + 1);
  for (unsigned i = 0; i <= available; ++i) {
    auto ptr = source->alloc<8>();
    bool reused = false;
    for (auto p : held) if (p == ptr) reused = true;
    ZuCheck(!reused && source->owned(ptr) == (i < available));
    remaining.push(ptr);
  }
  for (auto ptr : remaining) source->free(ptr);
  for (auto ptr : held) state.receiver->free(ptr);
  ZuCheck(source->stats().crossFrees.load_() == heldCount);
  ZuCheck(state.receiver->stats().crossFrees.load_() == 1);
}

void ZmHeapTest::cleanup()
{
  ZuTestScope(cleanup);
  struct State {
    const ZmHeapCache *removed = nullptr;
    unsigned removals = 0, frees = 0;
    bool ok = true;
  } state;
  static State *active;
  active = &state;
  m_hook = [](unsigned phase, const ZmHeapCache *cache, const void *ptr) {
    auto &s = *active;
    if (phase == Removed) {
      auto lookup = const_cast<ZmHeapLookup *>(static_cast<const ZmHeapLookup *>(ptr));
      s.ok &= cache->begin() && !cache->lookup() && !entries(*lookup, const_cast<ZmHeapCache *>(cache));
      s.removed = cache;
      ++s.removals;
      // Every remaining registered cache still has complete coverage.
      lookup->each_([&](ZmHeapCache *other) { s.ok &= covers(*lookup, other); });
    } else if (phase == Freeing && !cache->info().sharded) {
      s.ok &= s.removed == cache;
      ++s.frees;
    }
  };
  {
    ZmHeapMgr_ manager;
    constexpr auto id = "ZmHeapTest.Cleanup";
    manager.init(id, 0, 0, {4});
    auto singleton = manager.cache(id, 0, 64, 8, false);
    ZuCheck(singleton->begin() && !singleton->lookup());
    auto sharded = manager.cache(id, 0, 64, 8, true);
    ZuCheck(sharded->begin() && !sharded->lookup());
    manager.init(id, 0, 1, {2});
    manager.init(id, 1, 1, {2});
    auto shared = manager.cache(id, 1, 64, 16, false);
    ZuCheck(shared->begin() && shared->lookup());
    ZmHeapCache *empty = nullptr;
    ZmThread worker{[&] { empty = manager.cache(id, 1, 64, 16, false); },
      ZmThreadParams{}.partition(1)};
    worker.join();
    // Allocation failure must be injected before applying the first positive row.
    constexpr auto failedID = "ZmHeapTest.CleanupFail";
    auto failed = manager.cache(failedID, 0, 64, 8, false);
    m_fail = failed;
    manager.init(failedID, 0, 0, {2});
    ZuCheck(!failed->begin());
    manager.init(failedID, 1, 0, {2});
    ZmThread other{[&] { empty = manager.cache(failedID, 0, 64, 8, false); },
      ZmThreadParams{}.partition(1)};
    other.join();
    ZuCheck(!failed->begin() && failed->lookup() == empty->lookup());
  }
  m_hook = nullptr;
  ZuCheck(state.ok && state.removals == 4 && state.frees == 4);
}

void ZmHeapTest::run(const char *exe)
{
  ZuTestScope(run);
  ZuTestCall(zeroFirst);
  ZuTestCall(duplicates, exe);
  ZuTestCall(publication);
  ZuTestCall(bounds);
  ZuTestCall(activation<false>);
  ZuTestCall(activation<true>);
  ZuTestCall(ranges);
  ZuTestCall(rebuild);
  ZuTestCall(receivers);
  ZuTestCall(groups);
  ZuTestCall(transition, false);
  ZuTestCall(transition, true);
  ZuTestCall(cleanup);
}

int main(int argc, char **argv)
{
  if (argc == 2 && !strcmp(argv[1], "--duplicate"))
    return ZmHeapTest::duplicate(false);
  if (argc == 2 && !strcmp(argv[1], "--duplicate-fail"))
    return ZmHeapTest::duplicate(true);
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(ZmHeapTest::run, argv[0]);
  return 0;
}
