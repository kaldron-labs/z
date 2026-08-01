//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuArray.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmScratch.hh>

using namespace ZuTestUtil;

struct TestHeap {
  static inline unsigned allocs;
  static inline unsigned frees;

  static void *valloc(size_t size) {
    ++allocs;
    return Zm::alignedAlloc<alignof(uintptr_t)>(size);
  }
  static void vfree(const void *ptr) {
    ++frees;
    Zm::alignedFree(ptr);
  }
};

struct Object {
  static inline unsigned constructed;
  static inline unsigned destroyed;

  unsigned v;

  Object() : v{0} { ++constructed; }
  Object(unsigned v_) : v{v_} { ++constructed; }
  ~Object() { ++destroyed; }
};

void testArray()
{
  ZuTestScope(testArray);

  auto a = ZmScratch(int, 6);
  ZuCheck(a.size() == 6);
  ZuCheck(a.length() == 0);
  ZuCheck(a.span().length() == 0);
  ZuCheck(a.cspan().length() == 0);

  a << 1 << 2;
  ZuCheck(a.span().length() == 2);
  ZuCheck(a.cspan().length() == 2);
  auto span = a.span();
  span[1] = 3;
  ZuCheck(a[1] == 3);
  a[1] = 2;
  ZuCheck((a == ZuArray<int, 2>{1, 2}));
  ZuCheck(a.hash() == a.cspan().hash());

  a.unshift(0);
  ZuCheck(a.length() == 3);
  ZuCheck(a.shift() == 0);
  ZuCheck(a.pop() == 2);
  ZuCheck((a == ZuArray<int, 1>{1}));

  const int tail[] = {2, 3, 4, 5, 6, 7};
  a.append(tail);
  ZuCheck(a.length() == 6);
  ZuCheck(a[5] == 6);

  a.splice(1, 2);
  ZuCheck((a == ZuArray<int, 4>{1, 4, 5, 6}));

  a.null();
  a.length(3);
  for (unsigned n = a.length(), i = 0; i < n; i++) a[i] = i + 1;
  ZuCheck((a == ZuArray<int, 3>{1, 2, 3}));
}

void testLifetime()
{
  ZuTestScope(testLifetime);

  Object::constructed = Object::destroyed = 0;
  {
    auto a = ZmScratch(Object, 3);
    new (a.push()) Object{1};
    new (a.push()) Object{2};
    ZuCheck(a.length() == 2);
    ZuCheck(Object::constructed == 2);
    ZuCheck(Object::destroyed == 0);

    a.length(1);
    ZuCheck(Object::destroyed == 1);
    ZuCheck(a[0].v == 1);
  }
  ZuCheck(Object::constructed == 2);
  ZuCheck(Object::destroyed == 2);
}

void testString()
{
  ZuTestScope(testString);

  auto s = ZmScratch(char, 16);
  s << "abc" << 42;
  ZuCheck(s == "abc42");
  ZuCSpan span = s;
  ZuCheck(span == "abc42");
  ZuCheck(s.terminate());
  ZuCheck(!strcmp(s.data(), "abc42"));
}

void testVHeap()
{
  ZuTestScope(testVHeap);

  TestHeap::allocs = TestHeap::frees = 0;
  {
    unsigned n = ZmStackAvail();
    auto a = ZmScratch(uint8_t, n, TestHeap);
    ZuCheck(a.size() == n);
    ZuCheck(TestHeap::allocs == 1);
    a.push(42);
    ZuCheck(a[0] == 42);
  }
  ZuCheck(TestHeap::frees == 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testArray);
  ZuTestCall(testLifetime);
  ZuTestCall(testString);
  ZuTestCall(testVHeap);
  return 0;
}
