//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuMArray.hh>

using namespace ZuTestUtil;

struct ScaledArray : public ZuMArray<ScaledArray, ZuArray<int, 8>, int> {
  using Base = ZuMArray<ScaledArray, ZuArray<int, 8>, int>;
  using Base::Base;
  using Base::underlying;

  int get(uint64_t i) const { return underlying[i] * 2; }
  void set(uint64_t i, int v) { underlying[i] = v / 2; }
  uint64_t length() const { return underlying.length(); }
};

void testCRTPGetSetAndIteration()
{
  ZuTestScope(testCRTPGetSetAndIteration);

  ZuArray<int, 8> raw;
  raw << 1 << 2 << 3;

  ScaledArray a{raw};
  ZuCheck(a[0] == 2);
  ZuCheck(a[1] == 4);

  a[1] = 10;
  ZuCheck(a[1] == 10);
  ZuCheck(raw[1] == 5);

  const ScaledArray c{raw};
  int sum = 0;
  for (auto v : c) sum += int(v);
  ZuCheck(sum == (2 + 10 + 6));

  a.all<true>([](auto v) {
    v = int(v) + 2;
  });
  ZuCheck(a[0] == 4);
  ZuCheck(a[1] == 12);
  ZuCheck(a[2] == 8);
}

void testIteratorArithmetic()
{
  ZuTestScope(testIteratorArithmetic);

  ZuArray<int, 8> raw;
  raw << 4 << 6 << 8 << 10;
  ScaledArray a{raw};

  auto b = a.begin();
  auto e = a.end();

  ZuCheck((e - b) == 4);
  ZuCheck(int(*b) == 8);

  auto b2 = b;
  ++b2;
  ZuCheck((b2 - b) == 1);
  ZuCheck(int(*b2) == 12);

  --b2;
  ZuCheck(int(*b2) == 8);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCRTPGetSetAndIteration);
  ZuTestCall(testIteratorArithmetic);
  return 0;
}
