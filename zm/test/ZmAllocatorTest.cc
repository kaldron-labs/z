//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <vector>
#include <list>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAllocator.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmLocal.hh>
#include <zlib/ZmStackAvail.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmVHeap.hh>
#include <zlib/ZtcHeap.hh>

using namespace ZuTestUtil;

namespace {

template <typename Heap = ZuVoid>
struct FixedLazy_ : public Heap {
  uintptr_t value;
};
ZuDerive(FixedLazyHeap, (ZmHeap<"ZmAllocatorTest.FixedLazy", FixedLazy_<>>));
ZuDerive(FixedLazy, (FixedLazy_<FixedLazyHeap>));
ZuAssert(sizeof(FixedLazy) == sizeof(FixedLazy_<>));
ZuAssert(alignof(FixedLazy) == alignof(FixedLazy_<>));
ZuAssert((ZuIsSame<ZmHeapID<FixedLazy>,
  ZuStringT<"ZmAllocatorTest.FixedLazy">>{}));
using VHeapLazy =
  ZmVHeap<"ZmAllocatorTest.VHeapLazy", 16, 256, alignof(uintptr_t)>;

unsigned heapCount(ZuID id)
{
  unsigned n = 0;
  Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{
    [&n, id](Ztc::Heap *heap) {
      Ztc::HeapTelemetry data;
      heap->telemetry(data);
      if (data.id == id) ++n;
    }});
  return n;
}

}

void testLazyHeaps()
{
  ZuTestScope(testLazyHeaps);

  ZuCheck(!heapCount("ZmAllocatorTest.FixedLazy"));
  ZuCheck(!heapCount("ZmAllocatorTest.VHeapLazy"));

  auto fixed = new FixedLazy{};
  ZuCheck(heapCount("ZmAllocatorTest.FixedLazy") == 1);
  delete fixed;

  void *small = VHeapLazy::valloc(8);
  ZuCheck(heapCount("ZmAllocatorTest.VHeapLazy") == 1);
  void *large = VHeapLazy::valloc(64);
  ZuCheck(heapCount("ZmAllocatorTest.VHeapLazy") == 2);
  VHeapLazy::vfree(large);
  VHeapLazy::vfree(small);
}

template <bool Sharded>
void testHeapStats()
{
  ZuTestScope(testHeapStats);
  using Cache = ZmHeapCacheT<ZuStringT<"ZmAllocatorTest.Stats">,
    sizeof(uintptr_t), alignof(uintptr_t), Sharded>;
  auto cache = Cache::instance()->cache();
  const auto &global = cache->globalStats();
  const auto &stats = cache->stats();
  auto allocs = global.heapAllocs.load_();
  auto frees = global.heapFrees.load_();
  ZuCheck(!stats.allocs.load_() && !stats.frees.load_());
  ZuCheck(!stats.crossFrees.load_());
  ZuCheck(!stats.heapAllocs.load_() && !stats.heapFrees.load_());
  auto cached = Cache::alloc();
  auto first = Cache::alloc();
  auto second = Cache::alloc();
  ZuCheck(stats.allocs.load_() == 1 && global.heapAllocs.load_() == allocs + 2);
  ZuCheck(global.heapMax.load_() == 2);
  Cache::free(nullptr);
  Cache::free(first);
  Cache::free(second);
  Cache::free(cached);
  ZuCheck(stats.frees.load_() == 1 && global.heapFrees.load_() == frees + 2);
  auto reused = Cache::alloc();
  ZuCheck(reused == cached);
  Cache::free(reused);
  Ztc::HeapTelemetry data;
  cache->telemetry(data);
  ZuCheck(data.cacheAllocs == 2 && data.cacheFrees == 2);
  ZuCheck(stats.heapAllocs.load_() == 2 && stats.heapFrees.load_() == 2);
  ZuCheck(data.heapAllocs == 2 && data.heapFrees == 2);
  ZuCheck(data.globalHeapAllocs == allocs + 2 &&
    data.globalHeapFrees == frees + 2);
  ZuCheck(!data.crossFrees && data.globalHeapMax == 2 && !data.allocated());
}

void testSharedStats()
{
  ZuTestScope(testSharedStats);
  using Cache = ZmHeapCacheT<ZuStringT<"ZmAllocatorTest.Shared">,
    sizeof(uintptr_t), alignof(uintptr_t), false>;
  auto partition = ZmSelf()->partition();
  ZmHeapMgr::init("ZmAllocatorTest.Shared", partition, 0, {2});
  auto cache = Cache::instance()->cache();
  ZmSemaphore ready, resume;
  auto work = [cache, &ready, &resume] {
    ZuCheck(Cache::instance()->cache() == cache);
    auto first = Cache::alloc();
    auto second = Cache::alloc();
    ready.post();
    resume.wait();
    Cache::free(first);
    Cache::free(second);
    for (unsigned i = 0; i < 10000; ++i) {
      auto ptr = Cache::alloc();
      Cache::free(ptr);
    }
  };
  ZmThread first{work, ZmThreadParams{}.partition(partition)};
  ZmThread second{work, ZmThreadParams{}.partition(partition)};
  ready.wait();
  ready.wait();
  Ztc::HeapTelemetry data;
  cache->telemetry(data);
  ZuCheck(data.cacheAllocs == 2 && data.heapAllocs == 2);
  ZuCheck(data.globalHeapMax > 0 && data.globalHeapMax <= 2 && data.allocated() == 4);
  resume.post();
  resume.post();
  first.join();
  second.join();
  cache->telemetry(data);
  ZuCheck(data.cacheAllocs + data.heapAllocs == 20004);
  ZuCheck(data.cacheAllocs == data.cacheFrees);
  ZuCheck(data.heapAllocs == data.heapFrees);
  ZuCheck(data.globalHeapMax > 0 && data.globalHeapMax <= 2 &&
    !data.allocated() && !data.crossFrees);
}

void testCrossStats()
{
  ZuTestScope(testCrossStats);
  using Cache = ZmHeapCacheT<ZuStringT<"ZmAllocatorTest.Cross">,
    sizeof(uintptr_t), alignof(uintptr_t), false>;
  auto partition = ZmSelf()->partition();
  ZmHeapMgr::init("ZmAllocatorTest.Cross", partition, 0, {1});
  ZmHeapMgr::init("ZmAllocatorTest.Cross", partition + 1, 0, {1});
  auto cache = Cache::instance()->cache();
  auto cached = Cache::alloc();
  auto first = Cache::alloc();
  auto second = Cache::alloc();
  Ztc::HeapTelemetry other;
  ZmThread consumer{[cache, cached, first, second, &other] {
    auto remote = Cache::instance()->cache();
    ZuCheck(&remote->globalStats() == &cache->globalStats());
    ZuCheck(&remote->stats() != &cache->stats());
    Cache::free(cached);
    Cache::free(first);
    Cache::free(second);
    auto local = Cache::alloc();
    auto fallback = Cache::alloc();
    Cache::free(fallback);
    Cache::free(local);
    Cache::instance()->cache()->telemetry(other);
  }, ZmThreadParams{}.partition(partition + 1)};
  consumer.join();
  Ztc::HeapTelemetry data;
  cache->telemetry(data);
  ZuCheck(data.cacheAllocs == 1 && data.cacheFrees == 1 && data.crossFrees == 1);
  ZuCheck(data.heapAllocs == 2 && !data.heapFrees);
  ZuCheck(data.globalHeapAllocs == 3 && data.globalHeapFrees == 3 &&
    data.globalHeapMax == 2);
  ZuCheck(other.cacheAllocs == 1 && other.cacheFrees == 1 && !other.crossFrees);
  ZuCheck(other.heapAllocs == 1 && other.heapFrees == 3);
  ZuCheck(other.globalHeapAllocs == 3 && other.globalHeapFrees == 3 &&
    other.globalHeapMax == 2);
  ZuCheck(data.allocated() + other.allocated() == 0);
  auto reused = Cache::alloc();
  ZuCheck(reused == cached);
  Cache::free(reused);
}

void testIDStats()
{
  ZuTestScope(testIDStats);
  using Small = ZmHeapCacheT<ZuStringT<"ZmAllocatorTest.ID">, 16, 8, false>;
  using Large = ZmHeapCacheT<ZuStringT<"ZmAllocatorTest.ID">, 64, 64, true>;
  using Other = ZmHeapCacheT<ZuStringT<"ZmAllocatorTest.Other">, 16, 8, false>;
  auto small = Small::instance()->cache();
  auto large = Large::instance()->cache();
  auto other = Other::instance()->cache();
  ZuCheck(&small->globalStats() == &large->globalStats());
  ZuCheck(&small->globalStats() != &other->globalStats());
  ZuCheck(&small->stats() != &large->stats());
  auto first = Small::alloc();
  auto second = Large::alloc();
  ZuCheck(small->globalStats().heapAllocs.load_() == 2);
  ZuCheck(large->globalStats().heapMax.load_() == 2);
  ZuCheck(!other->globalStats().heapAllocs.load_());
  Ztc::HeapTelemetry data;
  small->telemetry(data);
  ZuCheck(data.heapAllocs == 1 && data.allocated() == 1);
  ZuCheck(data.globalHeapAllocs == 2 && !data.globalHeapFrees);
  large->telemetry(data);
  ZuCheck(data.heapAllocs == 1 && data.allocated() == 1);
  ZuCheck(data.globalHeapAllocs == 2 && !data.globalHeapFrees);
  Small::free(first);
  Large::free(second);
  ZuCheck(small->globalStats().heapFrees.load_() == 2);
  ZuCheck(small->stats().heapAllocs.load_() == 1 &&
    small->stats().heapFrees.load_() == 1);
  ZuCheck(large->stats().heapAllocs.load_() == 1 &&
    large->stats().heapFrees.load_() == 1);
  ZuCheck(!small->stats().allocs.load_());
  ZuCheck(!large->stats().frees.load_());
}

void testAllocatorWithSTL()
{
  ZuTestScope(testAllocatorWithSTL);

  using Alloc = ZmAllocator<int, "ZmAllocatorTest">;

  std::vector<int, Alloc> v{Alloc{}};
  v.push_back(1);
  v.push_back(2);
  v.push_back(3);
  ZuCheck(v.size() == 3);
  ZuCheck(v[0] == 1 && v[2] == 3);

  std::list<int, Alloc> l{Alloc{}};
  l.push_back(10);
  l.push_back(11);
  ZuCheck(l.size() == 2);

  std::vector<int, Alloc> copied = v;
  std::vector<int, Alloc> moved = ZuMv(copied);
  ZuCheck(moved.size() == 3);
}

void testAllocateDeallocate()
{
  ZuTestScope(testAllocateDeallocate);

  using Alloc = ZmAllocator<int, "ZmAllocatorTest">;
  Alloc alloc;

  int *single = alloc.allocate(1);
  ZuCheck(single);
  *single = 42;
  ZuCheck(*single == 42);
  alloc.deallocate(single, 1);

  int *multi = alloc.allocate(8);
  ZuCheck(multi);
  for (int i = 0; i < 8; i++) multi[i] = i;
  ZuCheck(multi[7] == 7);
  alloc.deallocate(multi, 8);
}

void testLocalAndStackAvail()
{
  ZuTestScope(testLocalAndStackAvail);

  struct Big {
    char data[1 << 20];
    int magic = 77;
  };

  ZuCheck(ZmStackAvail() > 0);

  auto localBig = ZmLocal(Big);
  ZuCheck(localBig);
  ZuCheck(localBig->magic == 77);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testLazyHeaps);
  ZmHeapMgr::init("ZmAllocatorTest.Stats", ZmSelf()->partition(), 0, {1});
  ZuTestCall(testHeapStats<false>);
  ZuTestCall(testHeapStats<true>);
  ZuTestCall(testSharedStats);
  ZuTestCall(testCrossStats);
  ZuTestCall(testIDStats);
  ZuTestCall(testAllocatorWithSTL);
  ZuTestCall(testAllocateDeallocate);
  ZuTestCall(testLocalAndStackAvail);
  return 0;
}
