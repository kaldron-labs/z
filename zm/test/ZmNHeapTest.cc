//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuPtr.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmHeap.hh>

template <typename T>
struct Foo : public ZmNHeap<Foo<T>> {
friend inline ZuStringT<"Foo"> ZmNHeapID(Foo *);

  T	value;
};

template <typename T>
struct Bar : public ZmNHeap<Bar<T>> {
friend inline ZuStringT<"Bar"> ZmNHeapID(Bar *);
friend inline ZuTrue ZmNHeapSharded(Bar *);

  T	value;
};

struct Plain : public ZmNHeap<Plain> {
  int	value;
};
struct PlainExt : public Plain {
  uint64_t	other;
};

struct EmptyID : public ZmNHeap<EmptyID> {
friend inline ZuStringT<""> ZmNHeapID(EmptyID *);

  int	value;
};

ZuAssert((ZuIsSame<ZmNHeapID_<Foo<int>>, ZuStringT<"Foo">>{}));
ZuAssert((ZuIsSame<ZmNHeapID_<Plain>, ZmNHeapDisable>{}));
ZuAssert((ZuIsSame<ZmNHeapID_<EmptyID>, ZmNHeapDisable>{}));
ZuAssert(!ZmNHeapSharded_<Foo<int>>{});
ZuAssert(ZmNHeapSharded_<Bar<int>>{});

using namespace ZuTestUtil;

void heapTest()
{
  ZuTestScope(heapTest);

  // Two cached objects expose overlap if small blocks cannot hold a free-list link.
  ZmHeapMgr::init("Foo", 0, 0, ZmHeapConfig{2, {}});
  using FooI = Foo<int>;
  ZuPtr<FooI> foo = new FooI();
  foo->value = 42;
  ZuPtr<FooI> other = new FooI();
  ZuCheck(foo->value == 42);
  ZuCheck(other->value == 0);
  ZuCheck(foo.ptr() != other.ptr());

  using BarI = Bar<int>;
  ZuPtr<BarI> bar = new BarI();
  ZuCheck(bar->value == 0);

  ZuPtr<Plain> plain = new Plain();
  ZuPtr<EmptyID> empty = new EmptyID();
  ZuCheck(plain->value == 0);
  ZuCheck(empty->value == 0);

  ZuPtr<PlainExt> ext = new PlainExt();
  ext->other = 42;
  ZuCheck(ext->value == 0);
  ZuCheck(ext->other == 42);
}

int main()
{
  ZuTestMain();
  ZuTestCall(heapTest);
}
