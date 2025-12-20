//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdio.h>

#include <zlib/ZmRef.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmAtomic.hh>

struct A {
  A(int i) : m_i(i) { }
  void operator()() { printf("A::operator() %d\n", m_i); }
  int	m_i;
};

struct B {
  B(int i) : m_i(i) { }
  int operator()() { printf("B::operator() %d\n", m_i); return m_i; }
  int	m_i;
};

int C() { printf("C() 44\n"); return 44; }

void D() { printf("D()\n"); }

struct E : public ZmPolymorph {
  E(int i) : m_i(i) { }
  virtual ~E() { }
  virtual void foo() = 0;
  virtual int bar() const = 0;
  static void bah() { printf("E::bah()\n"); }
  int	m_i;
};

struct E_ : public E {
  E_(int i) : E(i) { }
  void foo() { printf("E::foo() %d\n", m_i); }
  int bar() const { printf("E::bar() %d\n", m_i); return m_i; }
};

int F(int *i) { printf("F(%d)\n", *i); return *i; }

struct A1 {
  A1(int i) : m_i(i) { }
  void operator()(int j) { printf("A::operator(%d) %d\n", j, m_i); }
  int	m_i;
};

struct B1 {
  B1(int i) : m_i(i) { }
  int operator()(int j) { printf("B::operator(%d) %d\n", j, m_i); return m_i; }
  int	m_i;
};

int C1(int j) { printf("C1(%d) 44\n", j); return 44; }

void D1(int j) { printf("D(%d)\n", j); }

struct E1 {
  E1(int i) : m_i(i) { }
  void foo(int j) { printf("E::foo(%d) %d\n", j, m_i); }
  int bar(int j) const { printf("E::bar(%d) %d\n", j, m_i); return m_i; }
  static void bah(int j) { printf("E::bah(%d)\n", j); }
  int	m_i;
};

int F1(int *i, int j) { printf("F(%d, %d)\n", *i, j); return *i; }

struct A2 {
  A2(int i) : m_i(i) { }
  void operator()(int j, int k)
    { printf("A::operator(%d, %d) %d\n", j, k, m_i); }
  int	m_i;
};

struct B2 {
  B2(int i) : m_i(i) { }
  int operator()(int j, int k)
    { printf("B::operator(%d, %d) %d\n", j, k, m_i); return m_i; }
  int	m_i;
};

int C2(int j, int k) { printf("C2(%d, %d) 44\n", j, k); return 44; }

void D2(int j, int k) { printf("D(%d, %d)\n", j, k); }

struct E2 : public ZmPolymorph {
  E2(int i) : m_i(i) { }
  void foo(int j, int k) { printf("E::foo(%d, %d) %d\n", j, k, m_i); }
  int bar(int j, int k) const
    { printf("E::bar(%d, %d) %d\n", j, k, m_i); return m_i; }
  static void bah(int j, int k) { printf("E::bah(%d, %d)\n", j, k); }
  int	m_i;
};

struct E3 : public ZmPolymorph {
  template <int N> void foo(int i, int j) {
    printf("E3::foo<%d>(%d, %d)\n", N, i, j);
  }
};

int F2(int *i, int j, int k) { printf("F(%d, %d, %d)\n", *i, j, k); return *i; }

struct X {
  X() : m_i(42) { }
  X(const X &x) : m_i(x.m_i) { }
  X(X &&x) : m_i(x.m_i) { x.m_i = 0; }
  int m_i;
};

struct Base {
  Base(ZmAtomic<uint64_t> &i_) : i(i_) { }
  void foo_() { i.xch(i.load_() + 1); }
  void foo() { foo_(); }
  virtual void bar() { }
  ZmAtomic<uint64_t> &i;
};

struct Derived : public Base {
  Derived(ZmAtomic<uint64_t> &i) : Base(i) { }
  void bar() { Base::foo_(); }
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

void foo(X &x) { printf("%d\n", x.m_i); }

void ok(const char *s) { puts(s); }

void fail(const char *s) { puts(s); fflush(stdout); Zm::exit(1); }

#define CHECK(x) ((x) ? ok("OK  " #x) : fail("NOK " #x))

#if 0
template <typename L> void isStateless(const L &l) {
  printf("%d", (int)(ZmFn<>::template LambdaStateless<L, void>::OK));
}
auto fast() { return []{ putchar('a'); }; }
auto slow(char c) { return [c]() { putchar(c); }; }

extern "C" {
  void refFn(ZmObject *o);
  void derefFn(ZmObject *o);
};
#endif

int main()
{
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

    //printf("fa(A(42)) returned %d\n", (int)(int64_t)((*fa)()));
    //printf("fb(B(43)) returned %d\n", (int)(int64_t)((*fb)()));
    printf("fc(C) returned %d\n", int(fc()));
    fd();
    //printf("fe1(E(45)) returned %d\n", (int)(int64_t)((*fe1)()));
    //printf("fe2(E(46)) returned %d\n", (int)(int64_t)((*fe2)()));
    fe3();
    printf("ff(47) returned %d\n", (int)(int64_t)(ff()));

    A *a = new A(47);
    B *b = new B(48);
    ZmRef<E> e1 = new E_(49);
    ZmRef<E> e2 = new E_(50);
    // const E *e2c = e2;

    auto fap = ZmFn<>{a, ZmFnPtr<&A::operator()>{}};
    auto fbp = ZmFn<int()>{b, ZmFnPtr<&B::operator()>{}};
    // ZmFn<> fe1p = ZmFn<>{e1.ptr(, ZmFnPtr<&E::foo>{}});
    // ZmFn<> fe2p = ZmFn<>{ZmMkRef(e2c, ZmFnPtr<&E::bar>{}});

    fap();
    printf("fbp(new B(48)) returned %d\n", int(fbp()));
    // printf("fe1p(new E(49)) returned %d\n", (int)(int64_t)(fe1p()));
    // printf("fe2p(new E(50)) returned %d\n", (int)(int64_t)(fe2p()));

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

    //printf("fa(A1(42)) returned %d\n", (int)(int64_t)((*fa)(-42)));
    //printf("fb(B1(43)) returned %d\n", (int)(int64_t)((*fb)(-42)));
    printf("fc(C1) returned %d\n", int(fc(-42)));
    fd(-42);
    //printf("fe1(E1(45)) returned %d\n", (int)(int64_t)((*fe1)(-42)));
    //printf("fe2(E1(46)) returned %d\n", (int)(int64_t)((*fe2)(-42)));
    fe3(-42);
    printf("ff(47) returned %d\n", int(ff(-42)));

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
    printf("fbp(new B1(48)) returned %d\n", int(fbp(-42)));
    fe1p(-42);
    printf("fe2p(new E1(50)) returned %d\n", int(fe2p(-42)));

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

    //printf("fa(A2(42)) returned %d\n", (int)(int64_t)((*fa)(-42, -43)));
    //printf("fb(B2(43)) returned %d\n", (int)(int64_t)((*fb)(-42, -43)));
    printf("fc(C2) returned %d\n", int(fc(-42, -43)));
    fd(-42, -43);
    //printf("fe1(E2(45)) returned %d\n", (int)(int64_t)((*fe1)(-42, -43)));
    //printf("fe2(E2(46)) returned %d\n", (int)(int64_t)((*fe2)(-42, -43)));
    fe3(-42, -43);
    printf("ff(47) returned %d\n", int(ff(-42, -43)));

    A2 *a = new A2(47);
    B2 *b = new B2(48);
    ZmRef<E2> e1 = new E2(49);
    ZmRef<E2> e2 = new E2(50);
    const E2 *e2c = e2;

    auto fap = ZmFn<void(int, int)>{a, ZmFnPtr<&A2::operator()>{}};
    auto fbp = ZmFn<int(int, int)>{b, ZmFnPtr<&B2::operator()>{}};

    fap(-42, -43);
    printf("fbp(new B2(48)) returned %d\n", int(fbp(-42, -43)));

    CHECK(e1->refCount() == 1);
    CHECK(e2->refCount() == 1);

    {
      auto fe1p(ZmFn<void(int, int)>{e1.ptr(), ZmFnPtr<&E2::foo>{}});
      auto fe2p(ZmFn<int(int, int)>{ZmMkRef(e2c), ZmFnPtr<&E2::bar>{}});

      CHECK(e1->refCount() == 1);
      CHECK(e2->refCount() == 2);

      fe1p(-42, -43);
      printf("fe2p(new E2(50)) returned %d\n", int(fe2p(-42, -43)));
    }

    CHECK(e1->refCount() == 1);
    CHECK(e2->refCount() == 1);

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
      ZmFn<> foo = ZmFn<>::Lambda<ZmLambda_HeapID>::fn(
	  []{ puts("Hello World"); });
      foo();
#if 0
      printf("fast slow ");
      isStateless(fast());
      putchar(' ');
      isStateless(slow(' '));
      putchar('\n');
#endif
    }
    {
      auto foo = ZmFn<int()>([]{ puts("Hello World"); return 42; });
      printf("foo() %d (should be 42)\n", foo());
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
    CHECK(e->refCount() == 1);
    foo();
    const char *s = "Hello World";
    auto foo2 = ZmFn<>(e, [s](E_ *e) { puts(s); e->bar(); });
    CHECK(e->refCount() == 2);
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
    ZmFn<void(MoveOnly)> fn{[](MoveOnly m) { printf("%d\n", m.i); } };
    fn(MoveOnly());
    MoveOnly m;
    fn(ZuMv(m));
  }
  {
    ZmRef<E_> e = new E_(42);
    ZmFn<void(ZmAnyFn *)> fn{ZuMv(e), [](E_ *, ZmAnyFn *fn) {
	ZmRef<E_> e = fn->mvObject<E_>();
	CHECK(e->refCount() == 1);
      }};
    fn(&fn);
  }
  {
    ZmRef<E_> e = new E_(42);
    ZmFn<void()> fn{e, [](E_ *e) {
      CHECK(e->refCount() == 2);
    }};
    fn();
  }
  {
    ZmRef<E_> e = new E_(42);
    ZmFn<void()> fn{e.ptr(), [](E_ *e) {
      CHECK(e->refCount() == 1);
    }};
    fn();
  }
  {
    ZmRef<E_> e = new E_(42);
    ZmFn<> fn{ZmFn<>::mvFn(ZuMv(e), [](ZmRef<E_> e) {
	CHECK(e->refCount() == 1);
      })};
    fn();
  }
  {
    ZmFn<int()> fn{[]{ return -42; }};
    short x = fn();
    CHECK(x == -42);
  }
  {
    ZmAtomic<uint64_t> i;
    {
      Derived d(i);
      ZuTime begin = Zm::now();
      for (unsigned i = 0; i < 1000000000; i++) d.foo();
      ZuTime end = Zm::now(); end -= begin;
      puts((ZuCArray<80>{} << "direct call:\t" << end.interval() <<
	  "\t(" << ZuBox<uint64_t>(d.i) << ")").terminate());
    }
    {
      Derived d(i);
      ZmFn<> bar = ZmFn<>{&d, ZmFnPtr<&Base::foo>{}};
      ZuTime begin = Zm::now();
      for (unsigned i = 0; i < 1000000000; i++) bar();
      ZuTime end = Zm::now(); end -= begin;
      puts((ZuCArray<80>{} << "castFn:\t\t" << end.interval() <<
	  "\t(" << ZuBox<uint64_t>(d.i) << ")").terminate());
    }
    {
      Derived d(i);
      ZmFn<> baz = ZmFn<>(&d, [](Derived *d_) { d_->foo(); });
      ZuTime begin = Zm::now();
      for (unsigned i = 0; i < 1000000000; i++) baz();
      ZuTime end = Zm::now(); end -= begin;
      puts((ZuCArray<80>{} << "fast lambdaFn:\t" << end.interval() <<
	"\t(" << ZuBox<uint64_t>(d.i) << ")").terminate());
    }
    {
      Derived d(i);
      ZmFn<> baz = ZmFn<>([&d]() { d.foo(); });
      ZuTime begin = Zm::now();
      for (unsigned i = 0; i < 1000000000; i++) baz();
      ZuTime end = Zm::now(); end -= begin;
      puts((ZuCArray<80>{} << "slow lambdaFn:\t" << end.interval() <<
	"\t(" << ZuBox<uint64_t>(d.i) << ")").terminate());
    }
    {
      Derived d(i);
      Base *b = &d;
      ZuTime begin = Zm::now();
      for (unsigned i = 0; i < 1000000000; i++) b->bar();
      ZuTime end = Zm::now(); end -= begin;
      puts((ZuCArray<80>{} << "virtual fn:\t" << end.interval() <<
	"\t(" << ZuBox<uint64_t>(d.i) << ")").terminate());
    }
  }
}
