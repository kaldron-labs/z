//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <cstdint>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuArrayFn.hh>

using namespace ZuTestUtil;

struct Obj {
  Obj() : v{-1} { ++ctor; }
  Obj(int i) : v{i} { ++ctor; }
  Obj(const Obj &o) : v{o.v} { ++copy; }
  Obj(Obj &&o) : v{o.v} { o.v = -99; ++move; }
  Obj &operator =(const Obj &) = default;
  Obj &operator =(Obj &&) = default;
  ~Obj() { ++dtor; }

  int v;

  static int ctor;
  static int copy;
  static int move;
  static int dtor;
};

int Obj::ctor = 0;
int Obj::copy = 0;
int Obj::move = 0;
int Obj::dtor = 0;

void resetObjCounters()
{
  Obj::ctor = Obj::copy = Obj::move = Obj::dtor = 0;
}

void testNonPODOps()
{
  ZuTestScope(testNonPODOps);
  resetObjCounters();

  alignas(Obj) uint8_t aBuf[sizeof(Obj) * 2];
  alignas(Obj) uint8_t bBuf[sizeof(Obj) * 2];
  alignas(Obj) uint8_t cBuf[sizeof(Obj) * 2];
  alignas(Obj) uint8_t dBuf[sizeof(Obj) * 2];

  auto *a = reinterpret_cast<Obj *>(aBuf);
  auto *b = reinterpret_cast<Obj *>(bBuf);
  auto *c = reinterpret_cast<Obj *>(cBuf);
  auto *d = reinterpret_cast<Obj *>(dBuf);

  ZuArrayFn<Obj>::initElem(a + 0, 10);
  ZuArrayFn<Obj>::initElem(a + 1, 11);

  ZuArrayFn<Obj>::moveElems(b, a, 2);
  ZuCheck(b[0].v == 10);
  ZuCheck(b[1].v == 11);

  ZuArrayFn<Obj>::initElem(c + 0, 20);
  ZuArrayFn<Obj>::initElem(c + 1, 21);

  ZuArrayFn<Obj>::copyElems(d, c, 2);
  ZuCheck(d[0].v == 20);
  ZuCheck(d[1].v == 21);

  ZuArrayFn<Obj>::destroyElems(b, 2);
  ZuArrayFn<Obj>::destroyElems(c, 2);
  ZuArrayFn<Obj>::destroyElems(d, 2);

  ZuCheck(Obj::move >= 2);
  ZuCheck(Obj::copy >= 2);
  ZuCheck(Obj::dtor >= Obj::ctor + Obj::copy + Obj::move - 4);
}

void testPODOps()
{
  ZuTestScope(testPODOps);

  int v[] = { 1, 2, 3, 4 };
  ZuArrayFn<int>::moveElems(v + 1, v, 3); // overlap backward copy path
  ZuCheck(v[0] == 1 && v[1] == 1 && v[2] == 2 && v[3] == 3);

  ZuArrayFn<int>::moveElems(v, v + 1, 3); // overlap forward copy path
  ZuCheck(v[0] == 1 && v[1] == 2 && v[2] == 3);

  int same[] = { 5, 6, 7 };
  ZuArrayFn<int>::copyElems(same, same, 3); // explicit self-copy no-op
  ZuCheck(same[0] == 5 && same[1] == 6 && same[2] == 7);
}

void testCmpEqualsHashAndVoid()
{
  ZuTestScope(testCmpEqualsHashAndVoid);

  int a[] = { 3, 4, 5 };
  int b[] = { 3, 4, 5 };
  int c[] = { 3, 4, 6 };

  ZuCheck(ZuArrayFn<int>::equals(a, b, 3));
  ZuCheck(ZuArrayFn<int>::cmp(a, b, 3) == 0);
  ZuCheck(ZuArrayFn<int>::cmp(a, c, 3) < 0);
  ZuCheck(ZuArrayFn<int>::hash(a, 3) == ZuArrayFn<int>::hash(b, 3));

  ZuCheck(ZuArrayFn<void>::cmp(nullptr, nullptr, 123) == 0);
  ZuCheck(ZuArrayFn<void>::equals(nullptr, nullptr, 123));
  ZuCheck(ZuArrayFn<void>::hash(nullptr, 123) == 0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testNonPODOps);
  ZuTestCall(testPODOps);
  ZuTestCall(testCmpEqualsHashAndVoid);
  return 0;
}
