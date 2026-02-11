//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// backtrace test program

// this call stacks program -> shared libary -> shared library -> program
// (Windows: exe -> dll -> dll -> exe),  to test all 4 combinations of
// calling across executable modules

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmBackTrace.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

using namespace ZuTestUtil;

#ifdef ZDEBUG
#include <zlib/ZmTrap.hh>
#endif

extern
#ifdef _WIN32
ZuImport_API
#endif
ZmBackTrace xfoo(ZmBackTrace (*fn)());

template <bool X> struct Foo;
template <> struct Foo<1> {
  static ZmBackTrace d() {
    ZmBackTrace p, q;
    p.capture();
    q = p;
    return q;
  }
};

ZmBackTrace c() {
  return Foo<1>::d();
}

ZmBackTrace b() {
  return xfoo(c);
}

void a(ZmBackTrace &t) {
  t = b();
}

#ifdef ZmObject_DEBUG
struct A : public ZmObject { };

ZmRef<A> p() { ZmRef<A> a = new A(); a->ZmObject::debug(); return a; }

ZmRef<A> q() { return p(); }

ZmRef<A> r() { return q(); }

ZmRef<A> s() { return r(); }

void dump(void *, const void *referrer, const ZmBackTrace *bt)
{
  log(ZuBoxPtr(referrer).hex(), ':');
  log(*bt);
}
#endif

#ifdef ZDEBUG
void crash(int *x) { *x = 0; }

void bar(int *x) { crash(x); }

void foo() { bar(0); }
#endif

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  {
    ZmBackTrace t;
    a(t);
    ZuCheck(!!t);
    ZmBackTrace u = t;
    ZuCheck(u == t);
    log(t);
  }

#ifdef ZmObject_DEBUG
  ZmRef<A> a = s();
  ZuCheck(!!a);

  a->ZmObject::dump(0, &dump);
#endif

#ifdef ZDEBUG
  {
    ZmTrap::trap();
    //foo();
  }
#endif
}
