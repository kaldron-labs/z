//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTime.hh>

#include <zlib/ZmRef.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmSpecific.hh>

#define mb() __asm__ __volatile__("":::"memory")

template <typename... Ts>
void out(const Ts &...values) {
  (std::cout << ... << values) << '\n';
}

struct X : public ZmObject {
  X() : x(0) { }
  virtual ~X() { }
  virtual void helloWorld();
  void inc() { ++x; }
  unsigned x;
};

void X::helloWorld() { out("hello world"); }

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
      out(prefix, ": ", c->tid());
    else
      out(prefix, ": ", ZuCSpan(s.data(), s.length()));
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

    out("spawning 80 threads...");

    for (j = 0; j < 80; j++)
      r[j] = ZmThread{[sema]() { semPost(sema); }};

    for (j = 0; j < 80; j++) sema->wait();

    for (j = 0; j < 80; j++) r[j].join(0);

    out("80 threads finished");

    out("spawning 80 threads...");

    for (j = 0; j < 40; j++)
      r[j] = ZmThread{[sema]() { semWait(sema); }};

    for (j = 40; j < 80; j++)
      r[j] = ZmThread{[sema]() { semPost(sema); }};

    for (j = 0; j < 80; j++) r[j].join(0);

    out("80 threads finished");

    ZuTime start, end;

    start = Zm::now();

    for (j = 0; j < 1000000; j++) {
      sema->post();
      sema->wait();
    }

    end = Zm::now();
    end -= start;
    out("sem post/wait time: ",
      (ZuCArray<32>{} << end.interval()).data(),
      " / 1000000 = ",
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());

    delete sema;
  }

  {
    out("starting ZmPLock lock/unlock time test");

    ZmPLock lock;

    ZuTime start, end;

    start = Zm::now();

    for (j = 0; j < 1000000; j++) { lock.lock(); lock.unlock(); }

    end = Zm::now();
    end -= start;
    out("lock/unlock time: ",
      (ZuCArray<32>{} << end.interval()).data(),
      " / 1000000 = ",
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());
  }

  {
    out("starting ref/deref time test");

    ZmRef<ZmObject> l = new ZmObject;

    ZuTime start, end;

    start = Zm::now();

    for (j = 0; j < 1000000; j++) { l->ref(); mb(); }

    end = Zm::now();
    end -= start;
    out("ref time: ",
      (ZuCArray<32>{} << end.interval()).data(),
      " / 1000000 = ",
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());

    start = Zm::now();

    for (j = 0; j < 1000000; j++) { l->deref(); mb(); }

    end = Zm::now();
    end -= start;
    out("deref time: ",
      (ZuCArray<32>{} << end.interval()).data(),
      " / 1000000 = ",
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

    out("Meyers singleton time: ",
      (ZuCArray<32>{} << end.interval()).data(),
      " / 1000000 = ",
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());
    out("S() called ", S::m_j, " times"); S::m_j = 0;
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
    out("ZmSingleton::instance() time: ",
      (ZuCArray<32>{} << end.interval()).data(),
      " / 1000000 = ",
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());
    out("S() called ", S::m_j, " times"); S::m_j = 0;
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
    out("ZmSpecific::instance() time: ",
      (ZuCArray<32>{} << end.interval()).data(),
      " / 1000000 = ",
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());
    out("S() called ", S::m_j, " times"); S::m_j = 0;
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
    out("thread_local time: ",
      (ZuCArray<32>{} << end.interval()).data(),
      " / 1000000 = ",
      (ZuCArray<32>{} << (end.as_decimal() / ZuDecimal{1000000})).data());
    out("S() called ", S::m_j, " times"); S::m_j = 0;
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
  out("overall time: ",
    (ZuCArray<32>{} << overallEnd.interval()).data());
}
