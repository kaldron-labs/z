//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTime.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmScheduler.hh>

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

void usage_()
{
  std::cerr <<
    "Usage: zmheapbench2 COUNT SIZE NTHR [VERB]\n\n"
    "  COUNT\t- number of iterations\n"
    "  SIZE\t- size of heap\n"
    "  NTHR\t- number of threads\n"
    "  VERB\t- verbose (0 | 1 - defaults to 0)\n";
  Zm::exit(1);
}

int count;
int size;
int nthr;
ZmSemaphore sem;
ZmScheduler *sched;

int main(int argc, char **argv)
{
  if (argc < 4 || argc > 5) usage_();
  count = atoi(argv[1]);
  size = atoi(argv[2]);
  nthr = atoi(argv[3]);
  if (argc == 5) detailVerbose = atoi(argv[4]);
  if (!count || !nthr) usage_();
  for (int i = 0; i < nthr; i++)
    ZmHeapMgr::init("S", i, ZmHeapConfig{uint64_t(size)});
  ZmSchedParams params;
  params.id("sched").nThreads(nthr);
  for (int i = 0; i < nthr; i++)
    params.thread(i + 1).partition(i);
  ZmScheduler sched_{ZuMv(params)};
  sched = &sched_;
  sched->start();
  ZuTime start = Zm::now();
  for (int j = 0; j < count; j++)
    for (int i = 0; i < nthr; i++)
      sched->run([i, j]() {
	auto s = new S{i + j};
	sched->run([s]() {
	  delete s;
	  sem.post();
	}, ((i + 1) % nthr) + 1);
      }, i + 1);
  for (int k = 0, n = count * nthr; k < n; k++) sem.wait();
  sched->stop();
  ZuTime end = Zm::now();
  end -= start;
  out(end.sec(), '.', end.nsec());
  out(Ztc::heapCSV());}
