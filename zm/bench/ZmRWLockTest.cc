//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <iostream>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZuTime.hh>

struct Shared {
  unsigned		value = 0;
  ZmRWLock		lock;
  ZmAtomic<unsigned>	writeOps = 0;
  ZmAtomic<unsigned>	readOps = 0;
  ZmAtomic<unsigned>	tryReadMiss = 0;
  ZmAtomic<unsigned>	tryWriteMiss = 0;
};

void writer(Shared *shared, unsigned iterations)
{
  for (unsigned i = 0; i < iterations; i++) {
    ZmGuard<ZmRWLock> guard(shared->lock);
    ++shared->value;
    ++shared->writeOps;
  }
}

void reader(Shared *shared, unsigned iterations)
{
  unsigned checksum = 0;
  for (unsigned i = 0; i < iterations; i++) {
    if (shared->lock.readtrylock() == 0) {
      checksum += shared->value;
      ++shared->readOps;
      shared->lock.readunlock();
    } else
      ++shared->tryReadMiss;

    if (shared->lock.trylock() == 0) {
      ++shared->value;
      ++shared->writeOps;
      shared->lock.unlock();
    } else
      ++shared->tryWriteMiss;
  }
  if (checksum == 0xFFFFFFFFu)
    std::cout << "checksum guard: " << checksum << '\n';
}

void usage_()
{
  std::cout <<
    "Usage: ZmRWLockTest [ITERATIONS [READERS [WRITERS]]]\n"
    "  Defaults: ITERATIONS=100000 READERS=4 WRITERS=2\n";
  Zm::exit(1);
}

int main(int argc, char **argv)
{
  unsigned iterations = 100000;
  unsigned nReaders = 4;
  unsigned nWriters = 2;

  if (argc > 4) usage_();
  if (argc > 1) iterations = atoi(argv[1]);
  if (argc > 2) nReaders = atoi(argv[2]);
  if (argc > 3) nWriters = atoi(argv[3]);
  if (!iterations || !nReaders || !nWriters) usage_();

  Shared shared;
  unsigned totalThreads = nReaders + nWriters;
  ZmThread *threads = new ZmThread[totalThreads];

  ZuTime start = Zm::now();

  unsigned t = 0;
  for (unsigned i = 0; i < nReaders; i++, t++)
    threads[t] = ZmThread{[&shared, iterations]() { reader(&shared, iterations); }};
  for (unsigned i = 0; i < nWriters; i++, t++)
    threads[t] = ZmThread{[&shared, iterations]() { writer(&shared, iterations); }};

  for (t = 0; t < totalThreads; t++)
    threads[t].join();

  ZuTime elapsed = Zm::now();
  elapsed -= start;

  delete [] threads;

  unsigned writes = shared.writeOps;
  unsigned reads = shared.readOps;
  unsigned readMiss = shared.tryReadMiss;
  unsigned writeMiss = shared.tryWriteMiss;

  std::cout << "iterations: " << iterations << '\n'
	    << "readers: " << nReaders << '\n'
	    << "writers: " << nWriters << '\n'
	    << "value: " << shared.value << '\n'
	    << "writeOps: " << writes << '\n'
	    << "readOps: " << reads << '\n'
	    << "tryReadMiss: " << readMiss << '\n'
	    << "tryWriteMiss: " << writeMiss << '\n'
	    << "elapsed: " << elapsed.interval() << '\n';

  if (shared.value != writes) {
    std::cout << "FAILED\n";
    return 1;
  }

  std::cout << "OK\n";
  return 0;
}
