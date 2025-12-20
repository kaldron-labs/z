//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

/* test program */

#include <zlib/ZuLib.hh>

#include <stdlib.h>

#include <iostream>

#include <zlib/ZmRef.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmAtomic.hh>

void fail() { Zm::exit(1); }

void out(bool ok, ZuCSpan check, ZuCSpan diag) {
  std::cout
    << (ok ? "OK  " : "NOK ") << check << ' ' << diag
    << '\n' << std::flush;
}

#define CHECK_(x) out((x), #x, "")
#define CHECK(x, y) out((x), #x, y)

struct X : public ZmObject {
  X() : x(0) { }
  virtual ~X() { }
  virtual void helloWorld();
  void inc() { ++x; }
  unsigned x;
};

void X::helloWorld() { std::cout << "hello world\n" << std::flush; }

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

void Y::helloWorld() { std::cout << "hello world [Y]\n" << std::flush; }

ZmRef<X> foo(X *xPtr) { return(xPtr); }

struct O : public ZmObject {
  O() : referenced(0), dereferenced(0) { }
  ~O() { std::cout << "~O()\n" << std::flush; }
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
  ZmRef<X> x = new X;

  {
    ZmRef<X> nullPtr;
    ZmRef<X> nullPtr_;

    CHECK(!nullPtr, "null test 1");

    nullPtr = x;
    CHECK(nullPtr, "null test 2");

    nullPtr = 0;
    CHECK(!nullPtr, "null test 3");

    nullPtr_ = x;
    CHECK(nullPtr_, "null test 5");

    nullPtr_ = nullPtr;
    CHECK(!nullPtr_, "null test 6");

    nullPtr = x;
    CHECK(nullPtr, "null test 7");

    nullPtr = nullPtr_;
    CHECK(!nullPtr, "null test 8");

    nullPtr_ = (X *)0;
    CHECK(!nullPtr_, "null test 9");
  }

  {
    ZmRef<X> xPtr = foo(x);
    ZmRef<X> xPtr_ = foo(x);

    CHECK((X *)xPtr == &(*xPtr), "cast test 1");
    CHECK((X *)xPtr_ == &(*xPtr_), "cast test 2");
  }

  {
    ZmRef<X> xPtr(x);

    xPtr->helloWorld();

    ZmRef<X> xPtr2 = x;

    (*xPtr2).helloWorld();

    xPtr = x;

    CHECK(xPtr == xPtr2, "equality test 1");
    CHECK(xPtr == (ZmRef<X>)xPtr2, "equality test 2");

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

    CHECK((Z *)i()->val() == (Z *)z, "collection test");
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
    CHECK(z->m_z == 1234, "list1 test 1");
    z = list2.shiftVal();
    CHECK(z->m_z == 1234, "list2 test 1");
    list.del(z);
    z = list1.shiftVal();
    CHECK(z->m_z == 1234, "list1 test 2");
    z = list2.shiftVal();
    CHECK(z->m_z == 1234, "list2 test 2");

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
    std::cout << "z: "; z->debug();
    std::cout << "z2: "; z2->debug();
    std::cout << "z3: "; z3->debug();
    std::cout << std::flush;
#endif
    z = list1.shiftVal();
    CHECK(z->m_z == 1234, "list1 test 3");
    z = list2.popVal();
    CHECK(z->m_z == 3456, "list2 test 3");
    z = list1.shiftVal();
    CHECK(z->m_z == 2345, "list1 test 4");
    z = list2.popVal();
    CHECK(z->m_z == 2345, "list2 test 4");
    z = list1.shiftVal();
    CHECK(z->m_z == 3456, "list1 test 5");
    z = list2.popVal();
    CHECK(z->m_z == 1234, "list2 test 5");

    std::cout << "list3 iteration 1\n" << std::flush;
    {
      ZList::Iter iter(list3);

      while (z = iter.val())
	std::cout << "" << z->m_z << "\n" << std::flush;
    }

    std::cout << "list3 iteration 2\n" << std::flush;
    {
      ZList::Iter iter(list3);

      while (z = iter.val())
	std::cout << "" << z->m_z << "\n" << std::flush;
    }

    std::cout << "list3 iteration 3\n" << std::flush;
    {
      ZList::Iter iter(list3);

      while (z = iter.val())
	std::cout << "" << z->m_z << "\n" << std::flush;
    }

    std::cout << "list tests 1 ok\n" << std::flush;

    std::cout << "list2 count: " << list2.count_() << "\n" << std::flush;
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
	std::cout << z->data() << '\n' << std::flush;
    }
  }

  {
    ZmRef<O> p;
    {
      ZmRef<O> o = new O();

      CHECK_(o->referenced == 1 && !o->dereferenced);
      p = o;
    }
    CHECK_(p->referenced == 2 && p->dereferenced == 1);
    ZmRef<O> q = ZuMv(p);
    CHECK_(!p);
    CHECK_(q->referenced == 2 && q->dereferenced == 1);
  }

  {
    ZmRef<O> p = new O();
    CHECK_(p->referenced == 1 && !p->dereferenced);
    static auto fn = [](O *o) {
      CHECK_(o->referenced == 1);
      ZmRef<O> p = o;
      CHECK_(o->referenced == 2);
    };
    fn(p);
    CHECK_(p->referenced == 2 && p->dereferenced == 1);
  }
}
