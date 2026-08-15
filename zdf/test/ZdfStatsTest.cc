//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZdfStats.hh>

using namespace ZuTestUtil;

void describe(const Zdf::StatsTree<> &w) {
  std::cerr << "iteration\n";
  for (auto i = w.begin(), n = w.end(); i != n; ++i)
    std::cerr << i->first << ' ' << i->second << '\n';
  std::cerr << "\norder\n";
  for (unsigned i = 0,n = w.count(); i < n; i++) {
    auto j = w.order(i);
    std::cerr << j->first << ' ' << j->second << '\n';
  }
  std::cerr << "\nstats\n";
  std::cerr <<
    "count=" << ZuBoxed(w.count()) <<
    " min=" << ZuBoxed(w.minimum()) <<
    " max=" << ZuBoxed(w.maximum()) <<
    " mean=" << ZuBoxed(w.mean()).fp<-8>() <<
    " stddev=" << ZuBoxed(w.std()).fp<-8>() <<
    " median=" << ZuBoxed(w.median()).fp<-8>() <<
    " 80%=" << ZuBoxed(w.rank(0.80)) <<
    " 95%=" << ZuBoxed(w.rank(0.95)) << "\n\n";
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  using namespace Zdf;
  Zdf::StatsTree w;
  {
    describe(w);
    w.add(42);
    describe(w);
    w.add(42.1);
    describe(w);
    w.add(42);
    describe(w);
    w.add(42.2);
    describe(w);
    w.add(42);
    describe(w);
    w.add(42.3);
    describe(w);
    w.add(42.4);
    w.add(42.4);
    w.add(42.4);
    describe(w);
    w.del(42);
    w.del(42.4);
    describe(w);
    w.del(42);
    w.del(42.4);
    describe(w);
    w.del(42);
    w.del(42.4);
    describe(w);
    w.add(42);
    w.del(42.2);
    w.del(42.3);
    describe(w);
  }
  ZuCHECK(w.count() == 2, "final count");
  ZuCHECK(w.minimum() < w.maximum(), "final range");
  return 0;
}
