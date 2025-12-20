//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

/* test program */

#include <zlib/ZuLib.hh>

#include <stdio.h>
#include <stdlib.h>

#include <zlib/ZmLock.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmRandom.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

struct Lock : public ZmObject {
  Lock(unsigned rn) : m_rn(rn), m_nThreads(0) { }
  int incThreads() { return ++m_nThreads; }
  int decThreads() { return --m_nThreads; }
  void lock() { m_lock.lock(); }
  void unlock() { m_lock.unlock(); }
  ZmLock		m_lock;
  unsigned		m_rn;
  int			m_nThreads;
};

static ZmLock tableLock;
static unsigned nrecords = 0;
static ZmRef<Lock> *recordLocks = 0;

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
  if (!lock->decThreads()) recordLocks[lock->m_rn] = 0;
}

struct C {
  unsigned		id;
  pthread_t		tid;
};

static unsigned delay;
static volatile unsigned *cid = 0;

void breakpoint()
{
  puts("Aaaaaargh!");
  fflush(stdout);
}

extern "C" { void *run(void *); };
void *run(void *c_)
{
  C *c = (C *)c_;

  for (;;) {
    unsigned n = ZmRand::randExc(delay);
    unsigned rn = ZmRand::randExc(nrecords);
    ZmRef<Lock> lock = getlock(rn);
    cid[rn] = c->id;
    for (unsigned i = 0; i < n; i++) if (cid[rn] != c->id) breakpoint();
    lock->unlock();
    lock->unlock();
  }
  return 0;
}

void usage()
{
  fputs("Usage: ZmLockTest nthreads nrecords maxdelay\n", stderr);
  Zm::exit(1);
}

int main(int argc, char **argv)
{
  if (argc != 4) usage();
  int nthreads = atoi(argv[1]);
  nrecords = atoi(argv[2]);
  recordLocks = new ZmRef<Lock>[nrecords];
  cid = new volatile unsigned[nrecords];
  delay = atoi(argv[3]);
  auto c = static_cast<C *>(ZuAlloca(nthreads * sizeof(C), alignof(C)));
  for (int i = 0; i < nthreads; i++) {
    c[i].id = i + 1;
    pthread_create(&c[i].tid, 0, &run, (void *)&c[i]);
  }
  for (int i = 0; i < nthreads; i++)
    pthread_join(c[i].tid, 0);
  Zm::exit(0);
}
