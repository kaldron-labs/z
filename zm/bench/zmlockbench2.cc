//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <iostream>

#include <zlib/ZuLib.hh>

#include <zlib/ZmLock.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmRandom.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

struct Lock : public ZmObject {
  ZmLock		lock_;
  unsigned		rn;
  int			nThreads = 0;

  Lock(unsigned rn_) : rn(rn_) { }

  void lock() { lock_.lock(); }
  void unlock() { lock_.unlock(); }

  auto incThreads() { return ++nThreads; }
  auto decThreads() { return --nThreads; }
};

static ZmLock tableLock;
static unsigned nrecords = 0;
static ZmRef<Lock> *recordLocks = 0;
static unsigned iterations = 0;
static ZmAtomic<unsigned> failures = 0;

ZmRef<Lock> getlock(unsigned rn)
{
  ZmRef<Lock> lock;
  {
    ZmGuard<ZmLock> guard(tableLock);
    lock = recordLocks[rn];
    if (!lock) recordLocks[rn] = lock = new Lock(rn);
    lock->incThreads();
  }
  lock->lock();
  return lock;
}

void unlock(Lock *lock)
{
  lock->unlock();
  ZmGuard<ZmLock> guard(tableLock);
  if (!lock->decThreads()) recordLocks[lock->rn] = 0;
}

struct C {
  unsigned		id;
  pthread_t		tid;
};

static unsigned delay;
static volatile unsigned *cid = 0;

extern "C" { void *run(void *); };
void *run(void *c_)
{
  C *c = static_cast<C *>(c_);

  for (unsigned k = 0; k < iterations; k++) {
    unsigned n = delay ? ZmRand::randExc(delay) : 0;
    unsigned rn = ZmRand::randExc(nrecords);
    ZmRef<Lock> lock = getlock(rn);
    cid[rn] = c->id;
    for (unsigned i = 0; i < n; i++) {
      if (cid[rn] != c->id) {
	++failures;
	break;
      }
    }
    unlock(lock);
  }
  return 0;
}

void usage_()
{
  std::cout <<
    "Usage: zmlockbench2 [NTHREADS [NRECORDS [MAXDELAY [ITERATIONS]]]]\n"
    "  Defaults: NTHREADS=8 NRECORDS=128 MAXDELAY=256 ITERATIONS=10000\n";
  Zm::exit(1);
}

int main(int argc, char **argv)
{
  int nthreads = 8;
  nrecords = 128;
  delay = 256;
  iterations = 10000;

  if (argc > 5) usage_();
  if (argc > 1) nthreads = atoi(argv[1]);
  if (argc > 2) nrecords = atoi(argv[2]);
  if (argc > 3) delay = atoi(argv[3]);
  if (argc > 4) iterations = atoi(argv[4]);
  if (nthreads <= 0 || !nrecords || !iterations) usage_();

  auto c = static_cast<C *>(ZuAlloca(nthreads * sizeof(C), alignof(C)));

  recordLocks = new ZmRef<Lock>[nrecords];
  cid = new volatile unsigned[nrecords];
  for (unsigned i = 0; i < nrecords; i++) cid[i] = 0;

  ZuTime start = Zm::now();

  for (int i = 0; i < nthreads; i++) {
    c[i].id = i + 1;
    pthread_create(&c[i].tid, 0, &run, static_cast<void *>(&c[i]));
  }
  for (int i = 0; i < nthreads; i++)
    pthread_join(c[i].tid, 0);

  ZuTime elapsed = Zm::now();
  elapsed -= start;

  bool allReleased = true;
  for (unsigned i = 0; i < nrecords; i++) {
    if (recordLocks[i]) {
      allReleased = false;
      break;
    }
  }

  unsigned failureCount = failures;
  std::cout << "threads: " << nthreads << '\n'
	    << "records: " << nrecords << '\n'
	    << "delay: " << delay << '\n'
	    << "iterations: " << iterations << '\n'
	    << "failures: " << failureCount << '\n'
	    << "allReleased: " << (allReleased ? "true" : "false") << '\n'
	    << "elapsed: " << elapsed.interval() << '\n';

  delete [] recordLocks;
  delete [] cid;

  if (failureCount || !allReleased) {
    std::cout << "FAILED\n";
    return 1;
  }

  std::cout << "OK\n";
  return 0;
}
