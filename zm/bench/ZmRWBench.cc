//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// read/write lock stress test program

#include <stdlib.h>

#include <iostream>

#include <zlib/ZmGuard.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZuTime.hh>

struct Shared {
  unsigned	counter = 0;
  unsigned	readerChecksum = 0;
  ZmRWLock	rwLock;
};

void reader(Shared *shared, unsigned iterations)
{
  unsigned checksum = 0;
  for (unsigned i = 0; i < iterations; i++) {
    ZmReadGuard<ZmRWLock> guard(shared->rwLock);
    checksum += shared->counter;
  }
  ZmGuard<ZmRWLock> guard(shared->rwLock);
  shared->readerChecksum += checksum;
}

void writer(Shared *shared, unsigned iterations)
{
  for (unsigned i = 0; i < iterations; i++) {
    ZmGuard<ZmRWLock> guard(shared->rwLock);
    ++shared->counter;
  }
}

void usage_()
{
  std::cout <<
    "Usage: ZmRWBench [ITERATIONS [READERS [WRITERS]]]\n"
    "  Defaults: ITERATIONS=100000 READERS=2 WRITERS=4\n";
  Zm::exit(1);
}

int main(int argc, char **argv)
{
  unsigned iterations = 100000;
  unsigned nReaders = 2;
  unsigned nWriters = 4;

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

  unsigned expected = iterations * nWriters;
  std::cout << "iterations: " << iterations << '\n'
	    << "readers: " << nReaders << '\n'
	    << "writers: " << nWriters << '\n'
	    << "counter: " << shared.counter << '\n'
	    << "expected: " << expected << '\n'
	    << "readerChecksum: " << shared.readerChecksum << '\n'
	    << "elapsed: " << elapsed.interval() << '\n';

  if (shared.counter != expected) {
    std::cout << "FAILED\n";
    return 1;
  }

  std::cout << "OK\n";
  return 0;
}
