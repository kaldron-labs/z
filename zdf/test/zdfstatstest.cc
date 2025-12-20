//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZdfStats.hh>

void print(const char *s) {
  std::cout << s << '\n' << std::flush;
}
void print(const char *s, int64_t i) {
  std::cout << s << ' ' << i << '\n' << std::flush;
}
void ok(const char *s) { } // { print(s); }
void ok(const char *s, int64_t i) { } // { print(s, i); }
void fail(const char *s) { print(s); }
void fail(const char *s, int64_t i) { print(s, i); }
#define CHECK(x) ((x) ? ok("OK  " #x) : fail("NOK " #x))
#define CHECK2(x, y) ((x == y) ? ok("OK  " #x, x) : fail("NOK " #x, x))

void describe(const Zdf::StatsTree<> &w) {
  std::cout << "iteration\n";
  for (auto i = w.begin(); i != w.end(); ++i)
    std::cout << i->first << ' ' << i->second << '\n';
  std::cout << "\norder\n";
  for (unsigned i = 0,n = w.count(); i < n; i++) {
    auto j = w.order(i);
    std::cout << j->first << ' ' << j->second << '\n';
  }
  std::cout << "\nstats\n";
  std::cout <<
    "count=" << ZuBoxed(w.count()) <<
    " min=" << ZuBoxed(w.minimum()) <<
    " max=" << ZuBoxed(w.maximum()) <<
    " mean=" << ZuBoxed(w.mean()).fp<-8>() <<
    " stddev=" << ZuBoxed(w.std()).fp<-8>() <<
    " median=" << ZuBoxed(w.median()).fp<-8>() <<
    " 80%=" << ZuBoxed(w.rank(0.80)) <<
    " 95%=" << ZuBoxed(w.rank(0.95)) << "\n\n";
}

int main()
{
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
}
