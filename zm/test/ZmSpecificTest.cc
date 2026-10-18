//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// POSIX TLS records must retire on key deletion as well as thread exit.

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSpecific.hh>
#include <zlib/ZmThread.hh>

using namespace ZuTestUtil;

struct Record : ZmSpecific_Object {
  ZmAtomic<unsigned> *destroyed;
  explicit Record(ZmAtomic<unsigned> *value) : destroyed{value} { }
  ~Record() { ++*destroyed; }
};

static void current()
{
  ZuTestScope(current);
  ZmAtomic<unsigned> destroyed = 0;
  {
    ZmSpecific_Allocator<Record> allocator;
    ZuCheck(!allocator.get());
    auto record = new Record{&destroyed};
    ZuCheck(allocator.set(record) == 0);
    ZuCheck(allocator.get() == record && !destroyed.load_());
  }
  ZuCheck(destroyed == 1);
}

static void worker()
{
  ZuTestScope(worker);
  ZmAtomic<unsigned> destroyed = 0;
  {
    ZmSpecific_Allocator<Record> allocator;
    bool set = false;
    ZmThread thread{[&allocator, &destroyed, &set]() {
      set = allocator.set(new Record{&destroyed}) == 0;
    }};
    thread.join();
    ZuCheck(set && destroyed == 1 && !allocator.get());
    ZuCheck(allocator.set(new Record{&destroyed}) == 0);
  }
  ZuCheck(destroyed == 2);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(current);
  ZuTestCall(worker);
}
