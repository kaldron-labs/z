//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <vector>
#include <list>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAllocator.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmLocal.hh>
#include <zlib/ZmStackAvail.hh>
#include <zlib/ZmVHeap.hh>
#include <zlib/ZtcHeap.hh>

using namespace ZuTestUtil;

namespace {

template <typename Heap = ZuVoid>
struct FixedLazy_ : public Heap {
  uintptr_t value;
};
ZuDerive(FixedLazyHeap, (ZmHeap<"ZmAllocatorTest.FixedLazy", FixedLazy_<>>));
ZuDerive(FixedLazy, (FixedLazy_<FixedLazyHeap>));
ZuAssert(sizeof(FixedLazy) == sizeof(FixedLazy_<>));
ZuAssert(alignof(FixedLazy) == alignof(FixedLazy_<>));
ZuAssert((ZuIsSame<ZmHeapID<FixedLazy>,
  ZuStringT<"ZmAllocatorTest.FixedLazy">>{}));
using VHeapLazy =
  ZmVHeap<"ZmAllocatorTest.VHeapLazy", 16, 256, alignof(uintptr_t)>;

unsigned heapCount(ZuID id)
{
  unsigned n = 0;
  Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{
    [&n, id](Ztc::Heap *heap) {
      Ztc::HeapTelemetry data;
      heap->telemetry(data);
      if (data.id == id) ++n;
    }});
  return n;
}

}

void testLazyHeaps()
{
  ZuTestScope(testLazyHeaps);

  ZuCheck(!heapCount("ZmAllocatorTest.FixedLazy"));
  ZuCheck(!heapCount("ZmAllocatorTest.VHeapLazy"));

  auto fixed = new FixedLazy{};
  ZuCheck(heapCount("ZmAllocatorTest.FixedLazy") == 1);
  delete fixed;

  void *small = VHeapLazy::valloc(8);
  ZuCheck(heapCount("ZmAllocatorTest.VHeapLazy") == 1);
  void *large = VHeapLazy::valloc(64);
  ZuCheck(heapCount("ZmAllocatorTest.VHeapLazy") == 2);
  VHeapLazy::vfree(large);
  VHeapLazy::vfree(small);
}

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
  ZuTestCall(testLazyHeaps);
  ZuTestCall(testAllocatorWithSTL);
  ZuTestCall(testAllocateDeallocate);
  ZuTestCall(testLocalAndStackAvail);
  return 0;
}
