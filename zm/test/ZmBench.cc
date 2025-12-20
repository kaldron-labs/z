//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

/* test program */

#include <zlib/ZuLib.hh>

#include <stdio.h>

#include <iostream>

#include <zlib/ZuTime.hh>

#include <zlib/ZmRef.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmSpecific.hh>

#define mb() __asm__ __volatile__("":::"memory")

struct X : public ZmObject {
  X() : x(0) { }
  virtual ~X() { }
  virtual void helloWorld();
  void inc() { ++x; }
  unsigned x;
};

void X::helloWorld() { puts("hello world"); }

void semPost(ZmSemaphore *sema) {
  int i;

  ZmSpecific<X>::instance()->inc();
  for (i = 0; i < 10; i++) sema->post();
}

void semWait(ZmSemaphore *sema) {
  int i;

  ZmSpecific<X>::instance()->inc();
  for (i = 0; i < 10; i++) sema->wait();
}

struct S : public ZmObject {
  S() { m_i = 0; m_j++; }

  void foo() { m_i++; }

  static void meyers() {
    for (int i = 0; i < 100000; i++) {
      static ZmRef<S> s_ = new S();
      S *s = s_;
      s->foo();
      mb();
    }
  }

  static void singleton() {
    for (int i = 0; i < 100000; i++) {
      S *s = ZmSingleton<S>::instance();
      s->foo();
      mb();
    }
  }

  static void specific() {
    for (int i = 0; i < 100000; i++) {
      S *s = ZmSpecific<S>::instance();
      s->foo();
      mb();
    }
  }

  static void tls() {
    for (int i = 0; i < 100000; i++) {
      auto &s = ZmTLS([]() -> ZmRef<S> { return new S(); });
      s->foo();
      mb();
    }
  }

  int			m_i;
  static unsigned	m_j;
};

unsigned S::m_j = 0;

struct W {
  void fn(const char *prefix, const ZmThreadContext *c) {
    const ZmThreadName &s = c->name();
    if (!s)
      printf("%s: %d\n", prefix, (int)c->tid());
    else
      printf("%s: %.*s\n", prefix, s.length(), s.data());
  }
  void fn1(const ZmThreadContext *c) { fn("list1", c); }
  void fn2(const ZmThreadContext *c) { fn("list2", c); }
  void post() { m_sem.post(); }
  void wait() { m_sem.wait(); }
  ZmSemaphore				m_sem;
};

int main(int argc, char **argv)
{
  ZuTime overallStart, overallEnd;

  overallStart = Zm::now();

  ZmThread r[80];
  int j;

  {
    ZmSemaphore *sema = new ZmSemaphore;

    puts("spawning 80 threads...");

    for (j = 0; j < 80; j++)
      r[j] = ZmThread{[sema]() { semPost(sema); }};

    for (j = 0; j < 80; j++) sema->wait();

    for (j = 0; j < 80; j++) r[j].join(0);

    puts("80 threads finished");

    puts("spawning 80 threads...");

    for (j = 0; j < 40; j++)
      r[j] = ZmThread{[sema]() { semWait(sema); }};

    for (j = 40; j < 80; j++)
      r[j] = ZmThread{[sema]() { semPost(sema); }};

    for (j = 0; j < 80; j++) r[j].join(0);

    puts("80 threads finished");

    ZuTime start, end;

    start = Zm::now();

    for (j = 0; j < 1000000; j++) {
      sema->post();
      sema->wait();
    }

    end = Zm::now();
    end -= start;
    printf("sem post/wait time: %s / 1000000 = %s\n",
      (ZuCArray<32>{} << end.interval()).data(),
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());

    delete sema;
  }

  {
    puts("starting ZmPLock lock/unlock time test");

    ZmPLock lock;

    ZuTime start, end;

    start = Zm::now();

    for (j = 0; j < 1000000; j++) { lock.lock(); lock.unlock(); }

    end = Zm::now();
    end -= start;
    printf("lock/unlock time: %s / 1000000 = %s\n",
      (ZuCArray<32>{} << end.interval()).data(),
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());
  }

  {
    puts("starting ref/deref time test");

    ZmRef<ZmObject> l = new ZmObject;

    ZuTime start, end;

    start = Zm::now();

    for (j = 0; j < 1000000; j++) { l->ref(); mb(); }

    end = Zm::now();
    end -= start;
    printf("ref time: %s / 1000000 = %s\n",
      (ZuCArray<32>{} << end.interval()).data(),
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());

    start = Zm::now();

    for (j = 0; j < 1000000; j++) { l->deref(); mb(); }

    end = Zm::now();
    end -= start;
    printf("deref time: %s / 1000000 = %s\n",
      (ZuCArray<32>{} << end.interval()).data(),
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());
  }

  int n = Zm::getncpu();

  {
    ZuTime start, end;

    start = Zm::now();

    for (j = 0; j < n; j++)
      r[j] = ZmThread{S::meyers};
    for (j = 0; j < n; j++)
      r[j].join(0);

    end = Zm::now();
    end -= start;

    printf("Meyers singleton time: %s / 1000000 = %s\n",
      (ZuCArray<32>{} << end.interval()).data(),
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());
    printf("S() called %u times\n", S::m_j); S::m_j = 0;
  }

  {
    ZuTime start, end;

    start = Zm::now();

    for (j = 0; j < n; j++)
      r[j] = ZmThread{S::singleton};
    for (j = 0; j < n; j++)
      r[j].join(0);

    end = Zm::now();
    end -= start;
    printf("ZmSingleton::instance() time: %s / 1000000 = %s\n",
      (ZuCArray<32>{} << end.interval()).data(),
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());
    printf("S() called %u times\n", S::m_j); S::m_j = 0;
  }

  {
    ZuTime start, end;

    start = Zm::now();

    for (j = 0; j < n; j++)
      r[j] = ZmThread{S::specific};
    for (j = 0; j < n; j++)
      r[j].join(0);

    end = Zm::now();
    end -= start;
    printf("ZmSpecific::instance() time: %s / 1000000 = %s\n",
      (ZuCArray<32>{} << end.interval()).data(),
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());
    printf("S() called %u times\n", S::m_j); S::m_j = 0;
  }

  {
    ZuTime start, end;

    start = Zm::now();

    for (j = 0; j < n; j++)
      r[j] = ZmThread{S::tls};
    for (j = 0; j < n; j++)
      r[j].join(0);

    end = Zm::now();
    end -= start;
    printf("thread_local time: %s / 1000000 = %s\n",
      (ZuCArray<32>{} << end.interval()).data(),
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());
    printf("S() called %u times\n", S::m_j); S::m_j = 0;
  }

  {
    W w;
    for (j = 0; j < n; j++)
      r[j] = ZmThread{[w = &w]() { w->wait(); }};
    Zm::sleep(1);
    ZmThreadContextTLS::all(
	[&w](const ZmThreadContext *tc) { w.fn1(tc); });
    for (j = 0; j < n; j++) w.post();
    for (j = 0; j < n; j++) r[j].join(0);
    ZmThreadContextTLS::all(
	[&w](const ZmThreadContext *tc) { w.fn2(tc); });
  }

  overallEnd = Zm::now();
  overallEnd -= overallStart;
  printf("overall time: %s\n",
    (ZuCArray<32>{} << overallEnd.interval()).data());
}
