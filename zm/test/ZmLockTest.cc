//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

/* test program */

#include <zlib/ZuLib.hh>

#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <pthread.h>
#include <ck_spinlock.h>
#endif

#include <zlib/ZmPLock.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmSpinLock.hh>
#include <zlib/ZmSpecific.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZuTime.hh>

struct NoLock {
  ZuInline NoLock() { }
  ZuInline void lock() { }
  ZuInline void unlock() { }
};

#ifndef _WIN32
struct PThread {
  ZuInline PThread() { pthread_mutex_init(&m_lock, 0); }
  ZuInline ~PThread() { pthread_mutex_destroy(&m_lock); }
  ZuInline void lock() { pthread_mutex_lock(&m_lock); }
  ZuInline void unlock() { pthread_mutex_unlock(&m_lock); }

private:
  pthread_mutex_t	m_lock;
};

using FAS = ZmSpinLock;

struct Ticket {
  ZuInline Ticket() : m_lock(CK_SPINLOCK_TICKET_INITIALIZER) { }
  ZuInline void lock() { ck_spinlock_ticket_lock(&m_lock); }
  ZuInline void unlock() { ck_spinlock_ticket_unlock(&m_lock); }

private:
  ck_spinlock_ticket_t	m_lock;
};
#endif

struct C_ {
  unsigned		type;
  unsigned		id;
  pthread_t		tid;
  unsigned		count;
};
template <typename Lock> struct C : public C_ {
  Lock			*lock;
};

void *work()
{
  static unsigned count = 0;
  ++count;
  return (void *)&count;
}

template <typename Lock> void run_(void *c_)
{
  C<Lock> *c = (C<Lock> *)c_;

  for (unsigned i = 0, n = c->count; i < n; i++) {
    c->lock->lock();
    work();
    c->lock->unlock();
  }
}

struct Type { enum { NoLock = 0, PThread, ZmPLock, FAS, Ticket }; };

extern "C" { void *run(void *); };
void *run(void *c)
{
  switch (((C_ *)c)->type) {
    case Type::NoLock: run_<NoLock>(c); break;
    case Type::ZmPLock: run_<ZmPLock>(c); break;
#ifndef _WIN32
    case Type::PThread: run_<PThread>(c); break;
    case Type::FAS: run_<FAS>(c); break;
    case Type::Ticket: run_<Ticket>(c); break;
#endif
    default: break;
  }
  return 0;
}

void usage()
{
  fputs("Usage: ZmLockTest nthreads [count]\n", stderr);
  Zm::exit(1);
}

template <typename Lock> struct TypeCode { };
template <> struct TypeCode<NoLock> { enum { N = Type::NoLock }; };
template <> struct TypeCode<ZmPLock> { enum { N = Type::ZmPLock }; };
#ifndef _WIN32
template <> struct TypeCode<PThread> { enum { N = Type::PThread }; };
template <> struct TypeCode<FAS> { enum { N = Type::FAS }; };
template <> struct TypeCode<Ticket> { enum { N = Type::Ticket }; };
#endif

template <typename Lock>
void main_(const char *name, unsigned nthreads, unsigned count)
{
  Lock lock;
  auto c = static_cast<C<Lock> *>(
    ZuAlloca(nthreads * sizeof(C<Lock>), alignof(C<Lock>)));
  ZuTime begin = Zm::now();
  for (unsigned i = 0; i < nthreads; i++) {
    c[i].type = TypeCode<Lock>::N;
    c[i].id = i + 1;
    c[i].count = count;
    c[i].lock = &lock;
    pthread_create(&c[i].tid, 0, &run, (void *)&c[i]);
  }
  for (unsigned i = 0; i < nthreads; i++) pthread_join(c[i].tid, 0);
  ZuTime end = Zm::now(); end -= begin;
  std::cout << name << ":\t" << end.interval() << "\n";
}

int main(int argc, char **argv)
{
  if (argc < 2 || argc > 3) usage();
  int nthreads = atoi(argv[1]);
  int count = (argc > 2 ? atoi(argv[2]) : 100000000);
  if (nthreads <= 0 || count <= 0) usage();
  main_<NoLock>("NoLock", nthreads, count);
  main_<ZmPLock>("ZmPLock", nthreads, count);
#ifndef _WIN32
  main_<PThread>("PThread", nthreads, count);
  main_<FAS>("FAS", nthreads, count);
  main_<Ticket>("Ticket", nthreads, count);
#endif
  Zm::exit(0);
}
