//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuCmp.hh>

#include <zlib/ZmQueue.hh>

using namespace ZuTestUtil;

void testFIFOLIFO()
{
  ZuTestScope(testFIFOLIFO);

  ZmQueue<int> q{ZmQueueParams{}.initial(2).maxFrag(30)};

  q.push(1);
  q.push(2);
  q.push(3);
  ZuCheck(q.shift() == 1);

  q.unshift(0);
  ZuCheck(q.shift() == 0);

  ZuCheck(q.pop() == 3);
  ZuCheck(q.pop() == 2);
  ZuCheck(ZuCmp<int>::null(q.pop()));
}

void testFindDeleteAndDefrag()
{
  ZuTestScope(testFindDeleteAndDefrag);

  ZmQueue<int> q{ZmQueueParams{}.initial(8).maxFrag(0)};
  for (int i = 1; i <= 6; i++) q.push(i);

  ZuCheck(q.find(4) == 4);

  int *ptr = q.findPtr(4);
  ZuCheck(ptr && *ptr == 4);
  q.delPtr(ptr);
  ZuCheck(ZuCmp<int>::null(q.find(4)));

  ZuCheck(q.del(2) == 2);

  // maxFrag=0 triggers immediate compaction after interior deletes.
  ZuCheck(q.length_() == q.count_());
}

void testWrapAndMove()
{
  ZuTestScope(testWrapAndMove);

  ZmQueue<int> q{ZmQueueParams{}.initial(3).maxFrag(50)};
  bool ok = true;
  for (int i = 0; i < 32; i++) {
    q.push(i);
    if (q.shift() != i) ok = false;
  }
  ZuCheck(ok);

  q.push(7);
  q.push(8);

  ZmQueue<int> moved{ZuMv(q)};
  ZuCheck(moved.shift() == 7);
  ZuCheck(moved.shift() == 8);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testFIFOLIFO);
  ZuTestCall(testFindDeleteAndDefrag);
  ZuTestCall(testWrapAndMove);
  return 0;
}
