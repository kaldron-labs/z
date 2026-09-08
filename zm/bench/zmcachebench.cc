//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmCache.hh>
#include <zlib/ZmScratch.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmRBTree.hh>

#include <iostream>

struct Z {
  unsigned v;

  struct Traits : public ZuBaseTraits<Z> {
    enum { IsPrimitive = 0, IsReal = 0 };
  };
  friend Traits ZuTraitsType(Z *);
};

template <typename>
struct ZCmp {
  static int cmp(const Z *z1, const Z *z2) { return z1->v - z2->v; }
  static bool less(const Z *z1, const Z *z2) { return z1->v < z2->v; }
  static bool equals(const Z *z1, const Z *z2) { return z1->v == z2->v; }
  static bool null(const Z *z) { return !z; }
  static constexpr const Z *null() { return nullptr; }
};

ZmCacheDerive(ZCache, (ZuTuple<unsigned, Z>),
  ZmCacheKeyVal<ZuTupleAxor<0>(), ZuTupleAxor<1>(),
    ZmCacheLock<ZmPLock>>);
using ZNode = ZCache::Node;

ZmRBTreeKVDerive(ZTree, unsigned, Z, ZmRBTree_Defaults);

void backFill(ZTree &tree, unsigned cacheSize)
{
  for (unsigned i = 0; i < cacheSize; i++) tree.add(i, Z{i});
}

void find(ZCache &cache, ZTree &tree, unsigned offset, unsigned batchSize)
{
  for (unsigned i = 0; i < batchSize; i++)
    cache.find(
      offset + i,
      [](ZCache::NodeRef) { },
      [&tree]<typename L>(unsigned key, L l) {
	if (auto node = tree.find(key))
	  l(new ZNode{key, Z{node->val().v}});
	else
	  l(nullptr);
      });
}

void stats(const ZCache &cache)
{
  ZCache::Stats stats;
  cache.stats(stats);
  std::cout <<
    "count=" << stats.count <<
    " loads=" << stats.loads <<
    " misses=" << stats.misses <<
    " evictions=" << stats.evictions << '\n';
}

int main(int argc, char **argv)
{
  unsigned cacheSize = 100;
  unsigned batchSize = 100;
  unsigned nThreads = 2;
  unsigned nLoops = 2;
  ZuTime overallStart, overallEnd;

  if (argc > 1) cacheSize = batchSize = atoi(argv[1]);
  if (argc > 2) batchSize = atoi(argv[2]);
  if (argc > 3) nThreads = atoi(argv[3]);
  if (argc > 4) nLoops = atoi(argv[4]);

  auto threads = ZmScratch(ZmThread, nThreads);

  std::cout << "spawning " << nThreads << " threads..." << '\n';
  overallStart = Zm::now();

  ZCache cache{ZmHashParams{cacheSize}};
  ZTree tree;

  backFill(tree, cacheSize);

#if 0
  find(cache, tree, 0, cacheSize);
  find(cache, tree, 0, cacheSize);
#endif

  auto increment = cacheSize / nThreads;

  for (unsigned l = 0; l < nLoops; l++) {
    for (unsigned i = 0, j = 0; i < nThreads; i++, j += increment)
      new (threads.push()) ZmThread{[&cache, &tree, j, batchSize]() {
	find(cache, tree, j, batchSize);
      }};
    for (unsigned i = 0; i < nThreads; i++) {
      threads[i].join();
    }
    threads.null();
  }

  stats(cache);
}
