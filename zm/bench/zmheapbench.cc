//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#ifndef _WIN32
#include <alloca.h>
#endif

#include <new>
#include <iostream>
#include <vector>
#include <list>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTime.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmAllocator.hh>
#include <zlib/ZmVHeap.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmScratch.hh>

template <typename... Ts>
void out(const Ts &...values) {
  (std::cout << ... << values) << '\n';
}

static bool detailVerbose = false;

template <typename Heap = ZuVoid> struct S_ : public Heap {
  S_(int i) : m_i(i) { }
  ~S_() { m_i = -1; }
  void doit() {
    if (detailVerbose) out("hello world ", m_i);
    if (m_i < 0) abort();
  }
  int m_i;
};
ZuDerive(SHeap, (ZmHeap<"S", S_<>>));
ZuDerive(S, (S_<SHeap>));

using VectorHeap = ZmVHeap<"S_vector",
  sizeof(S) * ZmAllocator_DefltMin, sizeof(S) * ZmAllocator_DefltMax,
  alignof(S)>;

static unsigned count = 0;

void doit()
{
  out(*ZmSelf());
  for (unsigned i = 0; i < count; i++) {
    S *s = new S(i);
    s->doit();
    delete s;
  }
  {
    std::vector<S, ZmAllocator<S, "S_vector">> v;
    std::list<S, ZmAllocator<S, "S_list">> l;
    for (unsigned i = 0; i < count; i++) {
      v.emplace_back(i);
      l.emplace_back(i);
    }
  }
}

void usage_()
{
  std::cerr <<
    "Usage: zmheapbench COUNT SIZE NTHR [VERB]\n\n"
    "  COUNT\t- number of iterations\n"
    "  SIZE\t- size of heap\n"
    "  NTHR\t- number of threads\n"
    "  VERB\t- verbose (0 | 1 - defaults to 0)\n";
  Zm::exit(1);
}

int main(int argc, char **argv)
{
  ZuAssert((ZuIsSame<ZuStringT<"S">, ZmHeapID<S>>{}));
  if (argc < 4 || argc > 5) usage_();
  {
    out("ZmGrow sizes:");
    unsigned n = 1;
    for (unsigned i = 0; i < 18; i++) {
      auto m = ZmGrow(n, n + 1);
      out(n, " -> ", m);
      n = m;
    }
  }
  count = atoi(argv[1]);
  int size = atoi(argv[2]);
  int nthr = atoi(argv[3]);
  if (argc == 5) detailVerbose = atoi(argv[4]);
  if (!count || !nthr) usage_();
  for (int i = 0; i < nthr; i++) {
    ZmHeapMgr::init("S", i, 0, ZmHeapConfig{uint64_t(size)});
    // Retain the benchmark's decreasing capacity for larger vector blocks.
    for (unsigned j = 0; j < VectorHeap::NCaches; ++j)
      ZmHeapMgr::init("S_vector", i, j, ZmHeapConfig{uint64_t(size)>>j});
    ZmHeapMgr::init("S_list", i, 0, ZmHeapConfig{uint64_t(size)});
  }
  auto threads = ZmScratch(ZmThread, unsigned(nthr));
  if (nthr && !threads.data()) {
    std::cout << "ZmScratch() failed" << '\n';
    Zm::exit(1);
  }
  ZuTime start = Zm::now();
  for (int i = 0; i < nthr; i++)
    new (threads.push()) ZmThread{doit, ZmThreadParams{}.partition(i), i};
  for (int i = 0; i < nthr; i++) {
    threads[i].join();
  }
  ZuTime end = Zm::now();
  end -= start;
  out(end.sec(), '.', end.nsec());
  out(Ztc::heapCSV());
}
