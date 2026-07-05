//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmRef.hh>
#include <zlib/ZmCache.hh>
#include <zlib/ZmContext.hh>
#include <zlib/ZmPolymorph.hh>

using namespace ZuTestUtil;

struct Item : public ZmPolymorph {
  explicit Item(unsigned id_) : id{id_} { }
  unsigned id;
};

static unsigned itemKey(const ZmRef<Item> &v)
{
  return v->id;
}

using CacheNTP = ZmCacheKey<
  itemKey,
  ZmCacheLock<ZmPLock,
    ZmCacheHeapID<"ZmCacheTest">>>;

using Cache = ZmCache<ZmRef<Item>, CacheNTP>;
using CacheNoEvict = ZmCache<ZmRef<Item>, ZmCacheEvict<false, CacheNTP>>;

template <typename C>
static typename C::Node *mkNode(unsigned id)
{
  return new typename C::Node{ZmRef<Item>{new Item{id}}};
}

void testLoadMissAndStatsReset()
{
  ZuTestScope(testLoadMissAndStatsReset);

  Cache cache{ZmHashParams{}.bits(2).loadFactor(1.0)}; // minimum size is 4

  ZuCheck(!cache.add(mkNode<Cache>(1)));
  ZuCheck(!cache.add(mkNode<Cache>(2)));
  ZuCheck(!cache.add(mkNode<Cache>(3)));

  ZuCheck(cache.find(1U));
  ZuCheck(cache.find(2U));
  ZuCheck(cache.find(3U));
  ZuCheck(!cache.find(4U));

  cache.find(999U); // miss
  Cache::Stats before;
  cache.stats(before);
  ZuCheck(before.loads >= 1);
  ZuCheck(before.misses >= 1);
  ZuCheck(before.evictions == 0);

  Cache::Stats reset;
  cache.stats<true>(reset);

  Cache::Stats after;
  cache.stats(after);
  ZuCheck(after.loads == 0);
  ZuCheck(after.misses == 0);
}

void testNoEvictMode()
{
  ZuTestScope(testNoEvictMode);

  CacheNoEvict cache{ZmHashParams{}.bits(1).loadFactor(1.0)};

  cache.add(mkNode<CacheNoEvict>(10));
  cache.add(mkNode<CacheNoEvict>(11));
  cache.add(mkNode<CacheNoEvict>(12));

  CacheNoEvict::Stats stats;
  cache.stats(stats);
  ZuCheck(stats.count == 3);
  ZuCheck(stats.evictions == 0);
}

void testLoadDedupForConcurrentMissStyle()
{
  ZuTestScope(testLoadDedupForConcurrentMissStyle);

  CacheNoEvict cache{ZmHashParams{}.bits(2).loadFactor(1.0)};

  using NodeRef = CacheNoEvict::NodeRef;

  int loadCalls = 0;
  int callbackCount = 0;
  bool cb1ok = false;
  bool cb2ok = false;
  ZmFn<void(NodeRef)> pending;

  auto loadFn = [&loadCalls, &pending](unsigned key, auto complete) {
    ++loadCalls;
    pending = ZmFn<void(NodeRef)>{
      [complete = ZuMv(complete)](NodeRef node) mutable {
        complete(ZuMv(node));
      }
    };
  };

  cache.find(42U,
    [&cb1ok, &callbackCount](NodeRef node) {
      cb1ok = node && node->val()->id == 42;
      if (cb1ok) ++callbackCount;
    },
    loadFn);

  cache.find(42U,
    [&cb2ok, &callbackCount](NodeRef node) {
      cb2ok = node && node->val()->id == 42;
      if (cb2ok) ++callbackCount;
    },
    loadFn);

  ZuCheck(loadCalls == 1); // de-duplicated pending load
  ZuCheck(callbackCount == 0);
  ZuCheck(pending);

  pending(mkNode<CacheNoEvict>(42));

  ZuCheck(cb1ok);
  ZuCheck(cb2ok);
  ZuCheck(callbackCount == 2);

  auto found = cache.find(42U);
  ZuCheck(found && found->val()->id == 42);
}

void testContextSmoke()
{
  ZuTestScope(testContextSmoke);

  ZmRef<Item> item = new Item{77};
  ZmContext owned{item};
  ZuCheck(owned.object<Item>() == item.ptr());

  ZmContext raw{item.ptr()};
  ZuCheck(raw.object<Item>() == item.ptr());

  auto moved = owned.mvObject<Item>();
  ZuCheck(moved && moved->id == 77);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testLoadMissAndStatsReset);
  ZuTestCall(testNoEvictMode);
  ZuTestCall(testLoadDedupForConcurrentMissStyle);
  ZuTestCall(testContextSmoke);
  return 0;
}
