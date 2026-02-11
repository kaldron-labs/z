//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuLib.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmRef.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmAtomic.hh>

using namespace ZuTestUtil;

struct X : public ZmObject {
  X() : x(0) { }
  virtual ~X() { }
  virtual void helloWorld();
  void inc() { ++x; }
  unsigned x;
};

void X::helloWorld() { log("hello world"); }

struct Y : public X {
  virtual void helloWorld();
};

struct Z : public ZmObject { int m_z; };

template <typename>
struct ZCmp {
  static int cmp(const Z *z1, const Z *z2) { return z1->m_z - z2->m_z; }
  static bool less(const Z *z1, const Z *z2) { return z1->m_z < z2->m_z; }
  static bool equals(const Z *z1, const Z *z2) { return z1->m_z == z2->m_z; }
  static bool null(const Z *z) { return !z; }
  static const ZmRef<Z> &null() { static const ZmRef<Z> z; return z; }
};

ZuDerive(ZList, (ZmList<ZmRef<Z>, ZmListCmp<ZCmp> >));
ZuDerive(ZHash, (ZmHashKV<int, ZmRef<Z> >));

ZuDerive(ZList2, (ZmList<ZuCArray<20>, ZmListNode<ZuCArray<20>>>));

void Y::helloWorld() { log("hello world [Y]"); }

ZmRef<X> foo(X *xPtr) { return(xPtr); }

struct O : public ZmObject {
  O() : referenced(0), dereferenced(0) { }
  ~O() { log("~O()"); }
#ifdef ZmObject_DEBUG
  void ref(const void *referrer = 0) const {
    ++referenced;
    ZmObject::ref(referrer);
  }
#else
  void ref() const { ++referenced; }
#endif
#ifdef ZmObject_DEBUG
  bool deref(const void *referrer = 0) const {
    ++dereferenced;
    return ZmObject::deref(referrer);
  }
#else
  bool deref() const { return ++dereferenced >= referenced; }
#endif
  mutable unsigned referenced, dereferenced;
};

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZmRef<X> x = new X;

  {
    ZmRef<X> nullPtr;
    ZmRef<X> nullPtr_;

    ZuCHECK(!nullPtr, "null test 1");

    nullPtr = x;
    ZuCHECK(nullPtr, "null test 2");

    nullPtr = 0;
    ZuCHECK(!nullPtr, "null test 3");

    nullPtr_ = x;
    ZuCHECK(nullPtr_, "null test 5");

    nullPtr_ = nullPtr;
    ZuCHECK(!nullPtr_, "null test 6");

    nullPtr = x;
    ZuCHECK(nullPtr, "null test 7");

    nullPtr = nullPtr_;
    ZuCHECK(!nullPtr, "null test 8");

    nullPtr_ = (X *)0;
    ZuCHECK(!nullPtr_, "null test 9");
  }

  {
    ZmRef<X> xPtr = foo(x);
    ZmRef<X> xPtr_ = foo(x);

    ZuCHECK((X *)xPtr == &(*xPtr), "cast test 1");
    ZuCHECK((X *)xPtr_ == &(*xPtr_), "cast test 2");
  }

  {
    ZmRef<X> xPtr(x);

    xPtr->helloWorld();

    ZmRef<X> xPtr2 = x;

    (*xPtr2).helloWorld();

    xPtr = x;

    ZuCHECK(xPtr == xPtr2, "equality test 1");
    ZuCHECK(xPtr == (ZmRef<X>)xPtr2, "equality test 2");

    xPtr->helloWorld();

    X *xRealPtr = (X *)xPtr2;

    xRealPtr->helloWorld();
  }

  {
    ZmRef<Y> y = new Y;
    { ZmRef<Y> y2 = new Y; }

    ZmRef<Y> yPtr = y;
    ZmRef<X> xPtr = (ZmRef<X>)y;

    xPtr->helloWorld();
    yPtr->helloWorld();
    ((ZmRef<Y>)(Y *)(X *)xPtr)->helloWorld();
  }

  ZmRef<ZHash> hash = new ZHash(ZmHashParams().bits(8));
  ZmRef<Z> z = new Z;

  z->m_z = 1;

  hash->add(0, z);
  hash->add(1, z);
  hash->del(0);

  {
    ZHash::Iter i(*hash);

    ZuCHECK((Z *)i()->val() == (Z *)z, "collection test");
  }

  {
    ZList list;
    ZList list1;
    ZList list2;
    ZmRef<Z> z = new Z;

    z->m_z = 1234;

    list.add(z);
    list.add(z);
    list1.add(z);
    list2.add(z);
    list.del(z);
    list1.add(z);
    list2.add(z);
    z = list1.shiftVal();
    ZuCHECK(z->m_z == 1234, "list1 test 1");
    z = list2.shiftVal();
    ZuCHECK(z->m_z == 1234, "list2 test 1");
    list.del(z);
    z = list1.shiftVal();
    ZuCHECK(z->m_z == 1234, "list1 test 2");
    z = list2.shiftVal();
    ZuCHECK(z->m_z == 1234, "list2 test 2");

    ZList list3;
    ZmRef<Z> z2 = new Z, z3 = new Z;

    z2->m_z = 2345;
    z3->m_z = 3456;
    list1.add(z);
    list2.add(z);
    list3.add(z);
    list1.add(z2);
    list2.add(z2);
    list3.add(z2);
    list1.add(z3);
    list2.add(z3);
    list3.add(z3);
#ifdef ZmRef_DEBUG
    log("z:");
    z->debug();
    log("z2:");
    z2->debug();
    log("z3:");
    z3->debug();
#endif
    z = list1.shiftVal();
    ZuCHECK(z->m_z == 1234, "list1 test 3");
    z = list2.popVal();
    ZuCHECK(z->m_z == 3456, "list2 test 3");
    z = list1.shiftVal();
    ZuCHECK(z->m_z == 2345, "list1 test 4");
    z = list2.popVal();
    ZuCHECK(z->m_z == 2345, "list2 test 4");
    z = list1.shiftVal();
    ZuCHECK(z->m_z == 3456, "list1 test 5");
    z = list2.popVal();
    ZuCHECK(z->m_z == 1234, "list2 test 5");

    log("list3 iteration 1");
    {
      ZList::Iter iter(list3);

      while (z = iter.val())
	log("", z->m_z);
    }

    log("list3 iteration 2");
    {
      ZList::Iter iter(list3);

      while (z = iter.val())
	log("", z->m_z);
    }

    log("list3 iteration 3");
    {
      ZList::Iter iter(list3);

      while (z = iter.val())
	log("", z->m_z);
    }

    log("list tests 1 ok");
    log("list2 count: ", list2.count_());
  }

  {
    ZList2 list;
    list.addNode(new ZList2::Node("foo"));
    list.addNode(new ZList2::Node("bar"));
    list.addNode(new ZList2::Node("baz"));
    {
      ZList2::Iter iter(list);
      ZList2::NodeRef z;

      while (z = iter())
	log(z->data());
    }
  }

  {
    ZmRef<O> p;
    {
      ZmRef<O> o = new O();

      ZuCheck(o->referenced == 1 && !o->dereferenced);
      p = o;
    }
    ZuCheck(p->referenced == 2 && p->dereferenced == 1);
    ZmRef<O> q = ZuMv(p);
    ZuCheck(!p);
    ZuCheck(q->referenced == 2 && q->dereferenced == 1);
  }

  {
    ZmRef<O> p = new O();
    ZuCheck(p->referenced == 1 && !p->dereferenced);
    static auto fn = [](O *o) {
      ZuCheck(o->referenced == 1);
      ZmRef<O> p = o;
      ZuCheck(o->referenced == 2);
    };
    fn(p);
    ZuCheck(p->referenced == 2 && p->dereferenced == 1);
  }
}
