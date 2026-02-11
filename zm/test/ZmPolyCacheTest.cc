//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuDemangle.hh>
#include <zlib/ZuPrint.hh>

#include <zlib/ZmPolyCache.hh>

using namespace ZuTestUtil;

struct Foo_ {
  int i, j, k, l;

  template <typename S> void print(S &s) const {
    s << '{' << i << ',' << j << ',' << k << ',' << l << '}';
  }
  friend ZuPrintFn ZuPrintType(Foo_ *);
};

ZuStruct(Foo_,
    ((i), (Keys<0>)),
    ((j), ((Keys<1, 2>))),
    ((k), ((Keys<0, 1>))),
    ((l), (Keys<3>)));

ZuDerive(Cache, (ZmPolyCache<Foo_>));
using Foo = Cache::Node;

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  Cache cache("test");
  cache.add(new Foo{1,2,3,4});
  cache.add(new Foo{2,3,4,5});
  cache.add(new Foo{3,4,5,6});
  cache.add(new Foo{5,5,5,5});
  {
    auto x = cache.del<0>(ZuFwdTuple(1,3));
    ZuCheck(x);
    ZuCheck(x->i == 1 && x->j == 2 && x->k == 3 && x->l == 4);
    log(ZuDemangle<decltype(x)>{});
    log(ZuBoxPtr(static_cast<Foo *>(x)).hex());
    log(*x);
  }
  {
    log("iteration:");
    unsigned n = 0;
    cache.allSync([&](auto node, auto wake) {
      ++n;
      log(*node);
      wake();
    });
    ZuCheck(n == 3);
  }
  {
    auto x = cache.find<0>(ZuFwdTuple(2,4));
    ZuCheck(x);
    ZuCheck(x->i == 2 && x->j == 3 && x->k == 4 && x->l == 5);
    log("find<0>({2,4}): ", *x);
    x = cache.find<1>(ZuFwdTuple(3,4));
    ZuCheck(x);
    ZuCheck(x->i == 2 && x->j == 3 && x->k == 4 && x->l == 5);
    log("find<1>({3,4}): ", *x);
    x = cache.find<2>(ZuFwdTuple(3));
    ZuCheck(x);
    ZuCheck(x->i == 2 && x->j == 3 && x->k == 4 && x->l == 5);
    log("find<2>({3}): ", *x);
    x = cache.find<3>(ZuFwdTuple(5));
    ZuCheck(x);
    ZuCheck(x->i == 5 && x->j == 5 && x->k == 5 && x->l == 5);
    log("find<3>({5}): ", *x);
  }
}
