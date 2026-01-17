//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

/* test program */

#include <zlib/ZmHash.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

struct Connection : public ZmObject { };

ZuDerive(ConnHash, (ZmHashKV<int, ZmRef<Connection>, ZmHashLock<ZmPLock>>));

bool running = true;

struct TestObject {
  TestObject() { connHash = new ConnHash(); }

  void inserter() {
    printf("Starting inserter\n");
    while (running) {
      connHash->findAdd(15, ZmRef<Connection>(new Connection()));
    }
  }
  void remover() {
    printf("Starting remover\n");
    while (running) {
      connHash->del(15);
    }
  }
  void finder() {
    printf("Starting finder\n");
    while (running) {
      ZmRef<Connection> c = connHash->findVal(15);
    }
  }
  ZmRef<ConnHash>	connHash;
};

ZmSemaphore sem;

void stop() {
  running = false;
  sem.post();
}

int main(int argc, char *argv[])
{
  ZmTrap::sigintFn(stop);
  ZmTrap::trap();
  TestObject prog;

  ZmThread inserter{[&prog]() { prog.inserter(); }};
  ZmThread remover{[&prog]() { prog.remover(); }};
  ZmThread finder{[&prog]() { prog.finder(); }};

  sem.wait();

  inserter.join();
  remover.join();
  finder.join();

  puts("Caught Ctrl-C\n");

  return 0;
}
