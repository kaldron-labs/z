//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmRef.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmAtomic.hh>

using namespace ZuTestUtil;

struct A {
  A(int i) : m_i(i) { }
  void operator()() { log("A::operator() ", m_i); }
  int	m_i;
};

struct B {
  B(int i) : m_i(i) { }
  int operator()() { log("B::operator() ", m_i); return m_i; }
  int	m_i;
};

int C() { log("C() 44"); return 44; }

void D() { log("D()"); }

struct E : public ZmPolymorph {
  E(int i) : m_i(i) { }
  virtual ~E() { }
  virtual void foo() = 0;
  virtual int bar() const = 0;
  static void bah() { log("E::bah()"); }
  int	m_i;
};

struct E_ : public E {
  E_(int i) : E(i) { }
  void foo() { log("E::foo() ", m_i); }
  int bar() const { log("E::bar() ", m_i); return m_i; }
};

int F(int *i) { log("F(", *i, ')'); return *i; }

struct A1 {
  A1(int i) : m_i(i) { }
  void operator()(int j) { log("A::operator(", j, ") ", m_i); }
  int	m_i;
};

struct B1 {
  B1(int i) : m_i(i) { }
  int operator()(int j) { log("B::operator(", j, ") ", m_i); return m_i; }
  int	m_i;
};

int C1(int j) { log("C1(", j, ") 44"); return 44; }

void D1(int j) { log("D(", j, ')'); }

struct E1 {
  E1(int i) : m_i(i) { }
  void foo(int j) { log("E::foo(", j, ") ", m_i); }
  int bar(int j) const { log("E::bar(", j, ") ", m_i); return m_i; }
  static void bah(int j) { log("E::bah(", j, ')'); }
  int	m_i;
};

int F1(int *i, int j) { log("F(", *i, ", ", j, ')'); return *i; }

struct A2 {
  A2(int i) : m_i(i) { }
  void operator()(int j, int k)
    { log("A::operator(", j, ", ", k, ") ", m_i); }
  int	m_i;
};

struct B2 {
  B2(int i) : m_i(i) { }
  int operator()(int j, int k)
    { log("B::operator(", j, ", ", k, ") ", m_i); return m_i; }
  int	m_i;
};

int C2(int j, int k) { log("C2(", j, ", ", k, ") 44"); return 44; }

void D2(int j, int k) { log("D(", j, ", ", k, ')'); }

struct E2 : public ZmPolymorph {
  E2(int i) : m_i(i) { }
  void foo(int j, int k) { log("E::foo(", j, ", ", k, ") ", m_i); }
  int bar(int j, int k) const
    { log("E::bar(", j, ", ", k, ") ", m_i); return m_i; }
  static void bah(int j, int k) { log("E::bah(", j, ", ", k, ')'); }
  int	m_i;
};

struct E3 : public ZmPolymorph {
  template <int N> void foo(int i, int j) {
    log("E3::foo<", N, ">(", i, ", ", j, ')');
  }
};

int F2(int *i, int j, int k) { log("F(", *i, ", ", j, ", ", k, ')'); return *i; }

struct X {
  X() : m_i(42) { }
  X(const X &x) : m_i(x.m_i) { }
  X(X &&x) : m_i(x.m_i) { x.m_i = 0; }
  int m_i;
};

struct MoveOnly {
  MoveOnly() : i(42) { }
  ~MoveOnly() { }
  MoveOnly(const MoveOnly &) = delete;
  MoveOnly &operator =(const MoveOnly &) = delete;
  MoveOnly(MoveOnly &&m) : i(m.i) { m.i = 0; }
  MoveOnly &operator =(MoveOnly &&m) { i = m.i; m.i = 0; return *this; }
  int	i;
};

void foo(X &x) { log(x.m_i); }

#if 0
template <typename L> void isStateless(const L &l) {
  log(int(ZmFn<>::template LambdaStateless<L, void>::OK));
}
auto fast() { return []{ log('a'); }; }
auto slow(char c) { return [c]() { log(c); }; }

extern "C" {
  void refFn(ZmObject *o);
  void derefFn(ZmObject *o);
};
#endif

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  {
    //ZmRef<ZmFn<> > fa = ZmFn<>::fn(A(42));
    //ZmRef<ZmFn<> > fb = ZmFn<>::fn(B(43));
    auto fc = ZmFn<int()>{ZmFnPtr<&C>{}};
    auto fd = ZmFn<>{ZmFnPtr<&D>{}};
    //ZmRef<ZmFn<> > fe1 = ZmFn<>::fn<&E::foo>(E(45));
    //ZmRef<ZmFn<> > fe2 = ZmFn<>::fn<&E::bar>(E(46));
    auto fe3 = ZmFn<>{ZmFnPtr<&E::bah>{}};
    int i = 47;
    auto ff = ZmFn<int()>{&i, ZmFnPtr<&F>{}};

    //log("fa(A(42)) returned %d", (int)(int64_t)((*fa)()));
    //log("fb(B(43)) returned %d", (int)(int64_t)((*fb)()));
    log("fc(C) returned ", int(fc()));
    fd();
    //log("fe1(E(45)) returned %d", (int)(int64_t)((*fe1)()));
    //log("fe2(E(46)) returned %d", (int)(int64_t)((*fe2)()));
    fe3();
    log("ff(47) returned ", int(ff()));

    A *a = new A(47);
    B *b = new B(48);
    ZmRef<E> e1 = new E_(49);
    ZmRef<E> e2 = new E_(50);
    // const E *e2c = e2;

    auto fap = ZmFn<>{a, ZmFnPtr<&A::operator()>{}};
    auto fbp = ZmFn<int()>{b, ZmFnPtr<&B::operator()>{}};
    // ZmFn<> fe1p = ZmFn<>{e1.ptr(, ZmFnPtr<&E::foo>{}});
    // ZmFn<> fe2p = ZmFn<>{ZmRef(e2c, ZmFnPtr<&E::bar>{}});

    fap();
    log("fbp(new B(48)) returned ", int(fbp()));
    // log("fe1p(new E(49)) returned %d", (int)(int64_t)(fe1p()));
    // log("fe2p(new E(50)) returned %d", (int)(int64_t)(fe2p()));

    delete a;
    delete b;
  }
  {
    //ZmRef<ZmFn<void(int)> > fa = ZmFn<void(int)>::fn(A1(42));
    //ZmRef<ZmFn<void(int)> > fb = ZmFn<void(int)>::fn(B1(43));
    auto fc = ZmFn<int(int)>{ZmFnPtr<&C1>{}};
    auto fd = ZmFn<void(int)>{ZmFnPtr<&D1>{}};
    //ZmRef<ZmFn<void(int)> > fe1 = ZmFn<void(int)>::fn<&E1::foo>(E1(45));
    //ZmRef<ZmFn<void(int)> > fe2 = ZmFn<void(int)>::fn<&E1::bar>(E1(46));
    auto fe3 = ZmFn<void(int)>{ZmFnPtr<&E1::bah>{}};
    int i = 47;
    auto ff = ZmFn<int(int)>{&i, ZmFnPtr<&F1>{}};

    //log("fa(A1(42)) returned %d", (int)(int64_t)((*fa)(-42)));
    //log("fb(B1(43)) returned %d", (int)(int64_t)((*fb)(-42)));
    log("fc(C1) returned ", int(fc(-42)));
    fd(-42);
    //log("fe1(E1(45)) returned %d", (int)(int64_t)((*fe1)(-42)));
    //log("fe2(E1(46)) returned %d", (int)(int64_t)((*fe2)(-42)));
    fe3(-42);
    log("ff(47) returned ", int(ff(-42)));

    A1 *a = new A1(47);
    B1 *b = new B1(48);
    E1 *e1 = new E1(49);
    E1 *e2 = new E1(50);
    const E1 *e2c = e2;

    auto fap = ZmFn<void(int)>{a, ZmFnPtr<&A1::operator()>{}};
    auto fbp = ZmFn<int(int)>{b, ZmFnPtr<&B1::operator()>{}};
    auto fe1p = ZmFn<void(int)>{e1, ZmFnPtr<&E1::foo>{}};
    auto fe2p = ZmFn<int(int)>{e2c, ZmFnPtr<&E1::bar>{}};

    fap(-42);
    log("fbp(new B1(48)) returned ", int(fbp(-42)));
    fe1p(-42);
    log("fe2p(new E1(50)) returned ", int(fe2p(-42)));

    delete a;
    delete b;
    delete e1;
    delete e2;
  }
  {
    //ZmRef<ZmFn<void(int, int)> > fa = ZmFn<void(int, int)>::fn(A2(42));
    //ZmRef<ZmFn<void(int, int)> > fb = ZmFn<void(int, int)>::fn(B2(43));
    auto fc = ZmFn<int(int, int)>{ZmFnPtr<&C2>{}};
    auto fd = ZmFn<void(int, int)>{ZmFnPtr<&D2>{}};
    //ZmRef<ZmFn<void(int, int)> > fe1 = ZmFn<void(int, int)>::fn<&E2::foo>(E2(45));
    //ZmRef<ZmFn<void(int, int)> > fe2 = ZmFn<void(int, int)>::fn<&E2::bar>(E2(46));
    auto fe3 = ZmFn<void(int, int)>{ZmFnPtr<&E2::bah>{}};
    int i = 47;
    auto ff = ZmFn<int(int, int)>{&i, ZmFnPtr<&F2>{}};

    //log("fa(A2(42)) returned %d", (int)(int64_t)((*fa)(-42, -43)));
    //log("fb(B2(43)) returned %d", (int)(int64_t)((*fb)(-42, -43)));
    log("fc(C2) returned ", int(fc(-42, -43)));
    fd(-42, -43);
    //log("fe1(E2(45)) returned %d", (int)(int64_t)((*fe1)(-42, -43)));
    //log("fe2(E2(46)) returned %d", (int)(int64_t)((*fe2)(-42, -43)));
    fe3(-42, -43);
    log("ff(47) returned ", int(ff(-42, -43)));

    A2 *a = new A2(47);
    B2 *b = new B2(48);
    ZmRef<E2> e1 = new E2(49);
    ZmRef<E2> e2 = new E2(50);
    const E2 *e2c = e2;

    auto fap = ZmFn<void(int, int)>{a, ZmFnPtr<&A2::operator()>{}};
    auto fbp = ZmFn<int(int, int)>{b, ZmFnPtr<&B2::operator()>{}};

    fap(-42, -43);
    log("fbp(new B2(48)) returned ", int(fbp(-42, -43)));

    ZuCheck(e1->refCount() == 1);
    ZuCheck(e2->refCount() == 1);

    {
      auto fe1p(ZmFn<void(int, int)>{e1.ptr(), ZmFnPtr<&E2::foo>{}});
      auto fe2p(ZmFn<int(int, int)>{ZmRef(e2c), ZmFnPtr<&E2::bar>{}});

      ZuCheck(e1->refCount() == 1);
      ZuCheck(e2->refCount() == 2);

      fe1p(-42, -43);
      log("fe2p(new E2(50)) returned ", int(fe2p(-42, -43)));
    }

    ZuCheck(e1->refCount() == 1);
    ZuCheck(e2->refCount() == 1);

    delete a;
    delete b;
  }
  {
    ZmRef<E3> e3 = new E3();
    using TestFn = ZmFn<void(int, int)>;
    TestFn test = TestFn{e3, ZmFnPtr<&E3::foo<1>>{}};
  }
  {
    {
      auto foo = ZmFn<void(), ZmFnHeapID<"Foo">>::Lambda::fn([]{
	log("Hello World");
      });
      foo();
#if 0
      if (verbose) std::cerr << "fast slow ";
      isStateless(fast());
      if (verbose) std::cerr << ' ';
      isStateless(slow(' '));
      if (verbose) std::cerr << '\n';
#endif
    }
    {
      auto foo = ZmFn<int()>([]{ log("Hello World"); return 42; });
      log("foo() ", foo(), " (should be 42)");
    }
    ZmRef<E3> e3 = new E3();
    auto bar = ZmFn<>::fn([e3]() { e3->foo<1>(1, 1); });
    auto baz = ZmFn<>(e3, [](E3 *e3) { e3->foo<1>(1, 1); });
    bar();
    baz();
  }
  {
    ZmRef<E_> e = new E_(42);
    auto foo = ZmFn<>(e.ptr(), [](E_ *e) { e->foo(); });
    ZuCheck(e->refCount() == 1);
    foo();
    const char *s = "Hello World";
    auto foo2 = ZmFn<>(e, [s](E_ *e) { log(s); e->bar(); });
    ZuCheck(e->refCount() == 2);
    foo2();
  }
  {
    X v;
    X &vr = v;
    foo(vr);
    auto bar = ZmFn<void(X &)>{ZmFnPtr<&foo>{}};
    bar(vr);
    foo(v);
  }
  {
    ZmFn<void(MoveOnly)> fn{[](MoveOnly m) { log(m.i); } };
    fn(MoveOnly());
    MoveOnly m;
    fn(ZuMv(m));
  }
  {
    ZmRef<E_> e = new E_(42);
    ZmFn<void(ZmAnyFn *)> fn{ZuMv(e), [](E_ *, ZmAnyFn *fn) {
	ZmRef<E_> e = fn->mvObject<E_>();
	ZuCheck(e->refCount() == 1);
      }};
    fn(&fn);
  }
  {
    ZmRef<E_> e = new E_(42);
    ZmFn<void()> fn{e, [](E_ *e) {
      ZuCheck(e->refCount() == 2);
    }};
    fn();
  }
  {
    ZmRef<E_> e = new E_(42);
    ZmFn<void()> fn{e.ptr(), [](E_ *e) {
      ZuCheck(e->refCount() == 1);
    }};
    fn();
  }
  {
    ZmRef<E_> e = new E_(42);
    ZmFn<> fn{ZmFn<>::mvFn(ZuMv(e), [](ZmRef<E_> e) {
	ZuCheck(e->refCount() == 1);
      })};
    fn();
  }
  {
    ZmFn<int()> fn{[]{ return -42; }};
    short x = fn();
    ZuCheck(x == -42);
  }
  {
    // null callback invocation should be a no-op
    ZmFn<> fn;
    ZuCheck(!fn);
    fn();
  }
  {
    ZmFn<> fn{[]{}};
    ZuCheck(!!fn);
    fn = {};
    ZuCheck(!fn);
    fn();
  }
  {
    ZmFn<int()> fn{[]{ return 42; }};
    ZuCheck(fn() == 42);
    fn = {};
    ZuCheck(!fn);
    ZuCheck(fn() == 0);
  }
  {
    // move-from safety
    ZmFn<int()> fn{[]{ return 7; }};
    ZmFn<int()> moved{ZuMv(fn)};
    ZuCheck(!fn);
    ZuCheck(moved() == 7);
  }
  {
    // bound member pointer vs unbound/lambda parity
    E_ e{99};
    auto bound = ZmFn<int()>{&e, ZmFnPtr<&E_::bar>{}};
    auto unbound = ZmFn<int()>{[&e]{ return e.bar(); }};
    ZuCheck(bound() == unbound());
  }
}
