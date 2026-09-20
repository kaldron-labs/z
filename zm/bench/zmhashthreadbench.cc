//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <iostream>

#include <zlib/ZuDerive.hh>

#include <zlib/ZmHash.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmThread.hh>

struct Connection : public ZmObject { };

// This benchmark measures concurrent replacement of shared connections.
ZmHashKVDerive(ConnHash, int, ZmRef<Connection>, (ZmHashLock<ZmPLock>));

struct TestObject {
  TestObject() : connHash{new ConnHash()} { }

  void inserter(unsigned count) {
    for (unsigned i = 0; i < count; i++) {
      connHash->findAdd(15, ZmRef<Connection>(new Connection()));
      ++inserted;
    }
  }
  void remover(unsigned count) {
    for (unsigned i = 0; i < count; i++) {
      connHash->del(15);
      ++removed;
    }
  }
  void finder(unsigned count) {
    for (unsigned i = 0; i < count; i++) {
      if (connHash->findVal(15))
	++foundHits;
      ++found;
    }
  }

  ZmRef<ConnHash>	connHash;
  ZmAtomic<unsigned>	inserted = 0;
  ZmAtomic<unsigned>	removed = 0;
  ZmAtomic<unsigned>	found = 0;
  ZmAtomic<unsigned>	foundHits = 0;
};

void usage_()
{
  std::cout <<
    "Usage: zmhashthreadbench [ITERATIONS]\n"
    "  ITERATIONS defaults to 100000\n";
  Zm::exit(1);
}

int main(int argc, char **argv)
{
  unsigned iterations = 100000;

  if (argc > 2) usage_();
  if (argc == 2) {
    iterations = atoi(argv[1]);
    if (!iterations) usage_();
  }

  TestObject prog;

  ZuTime start = Zm::now();

  ZmThread inserter{[&prog, iterations]() { prog.inserter(iterations); }};
  ZmThread remover{[&prog, iterations]() { prog.remover(iterations); }};
  ZmThread finder{[&prog, iterations]() { prog.finder(iterations); }};

  inserter.join();
  remover.join();
  finder.join();

  ZuTime elapsed = Zm::now();
  elapsed -= start;

  unsigned inserted = prog.inserted;
  unsigned removed = prog.removed;
  unsigned found = prog.found;
  unsigned foundHits = prog.foundHits;
  unsigned remaining = prog.connHash->count_();

  std::cout << "iterations: " << iterations << '\n'
	    << "inserted: " << inserted << '\n'
	    << "removed: " << removed << '\n'
	    << "found: " << found << '\n'
	    << "foundHits: " << foundHits << '\n'
	    << "remaining: " << remaining << '\n'
	    << "elapsed: " << elapsed.interval() << '\n';

  if (inserted != iterations || removed != iterations || found != iterations ||
      foundHits > found || remaining > 1) {
    std::cout << "FAILED\n";
    return 1;
  }

  std::cout << "OK\n";
  return 0;
}
