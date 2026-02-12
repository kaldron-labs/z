//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <vector>
#include <list>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAllocator.hh>
#include <zlib/ZmLocal.hh>
#include <zlib/ZmStackAvail.hh>

using namespace ZuTestUtil;

void testAllocatorWithSTL()
{
  ZuTestScope(testAllocatorWithSTL);

  using Alloc = ZmAllocator<int, "ZmAllocatorTest">;

  std::vector<int, Alloc> v{Alloc{}};
  v.push_back(1);
  v.push_back(2);
  v.push_back(3);
  ZuCheck(v.size() == 3);
  ZuCheck(v[0] == 1 && v[2] == 3);

  std::list<int, Alloc> l{Alloc{}};
  l.push_back(10);
  l.push_back(11);
  ZuCheck(l.size() == 2);

  std::vector<int, Alloc> copied = v;
  std::vector<int, Alloc> moved = ZuMv(copied);
  ZuCheck(moved.size() == 3);
}

void testAllocateDeallocate()
{
  ZuTestScope(testAllocateDeallocate);

  using Alloc = ZmAllocator<int, "ZmAllocatorTest">;
  Alloc alloc;

  int *single = alloc.allocate(1);
  ZuCheck(single);
  *single = 42;
  ZuCheck(*single == 42);
  alloc.deallocate(single, 1);

  int *multi = alloc.allocate(8);
  ZuCheck(multi);
  for (int i = 0; i < 8; i++) multi[i] = i;
  ZuCheck(multi[7] == 7);
  alloc.deallocate(multi, 8);
}

void testLocalAndStackAvail()
{
  ZuTestScope(testLocalAndStackAvail);

  struct Big {
    char data[1 << 20];
    int magic = 77;
  };

  ZuCheck(ZmStackAvail() > 0);

  auto localBig = ZmLocal(Big);
  ZuCheck(localBig);
  ZuCheck(localBig->magic == 77);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testAllocatorWithSTL);
  ZuTestCall(testAllocateDeallocate);
  ZuTestCall(testLocalAndStackAvail);
  return 0;
}
