//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// intrusive globals (singletons) - used by ZmSingleton / ZmSpecific

#ifndef _WIN32
#include <alloca.h>
#endif

// #include <stdio.h>

#include <zlib/ZmGlobal.hh>

#ifdef ZDEBUG
#include <zlib/ZuVStream.hh>
#endif

// statically-initialized spinlock to guard initial singleton registration
// and cleanup at exit; little if any contention is anticipated; access
// intended to be exceptional, intermittent, almost entirely during
// process startup and shutdown
static uint32_t ZmGlobal_lock = 0;
#define lock() \
  auto lock = \
    ZuLaunder(reinterpret_cast<ZmAtomic<uint32_t> *>(&ZmGlobal_lock)); \
  while (ZuUnlikely(lock->cmpXch(1, 0))) Zm::yield()
#define unlock() (*lock = 0)

static uint32_t ZmGlobal_atexit_ = 0;
using ZmGlobalPtr = ZmGlobal *;
static ZmGlobalPtr ZmGlobal_list[ZmCleanup::N] = { 0 };

// atexit handler
ZmExtern void ZmGlobal_atexit()
{
  lock();
  ZmGlobal_atexit_ = 0;
  unsigned n = 0;
  for (unsigned i = 0; i < ZmCleanup::N; i++)
    for (ZmGlobal *g = ZmGlobal_list[i]; g; g = g->m_next) ++n;
  if (ZuUnlikely(!n)) { unlock(); return; }
  auto globals = static_cast<ZmGlobal **>(
    ZuAlloca(n * sizeof(ZmGlobal *), alignof(ZmGlobal)));
  if (ZuUnlikely(!globals)) { unlock(); return; }
  unsigned o = 0;
  for (unsigned i = 0; i < ZmCleanup::N; i++) {
    for (ZmGlobal *g = ZmGlobal_list[i]; g; g = g->m_next) {
      if (ZuUnlikely(o >= n)) { unlock(); return; }
      globals[o++] = g;
    }
    ZmGlobal_list[i] = 0;
  }
  unlock();
  // do not call dtor with lock held
  for (unsigned i = 0; i < n; i++) {
    ZmGlobal *g = globals[i];
    delete g;
  }
}

// singleton registration - normally only called once per type
// (on Windows, will be called once per type per DLL/EXE that uses it)
ZmGlobal *ZmGlobal::add(
    std::type_index type, unsigned level, ZmGlobal *(*ctor)())
{
  // printf("ZmGlobal::add()\n"); fflush(stdout);
  lock();
  if (ZuUnlikely(!ZmGlobal_atexit_)) {
    ZmGlobal_atexit_ = 1;
    ::atexit(ZmGlobal_atexit);
  }
  ZmGlobal *c = nullptr;
  ZmGlobal *g;
retry:
  for (g = ZmGlobal_list[level]; g; g = g->m_next)
    if (g->m_type == type) {
      unlock(); // do not call dtor with lock held
      // printf("ZmGlobal::add() returning existing instance %s %p\n", g->m_name, g); fflush(stdout);
      if (ZuUnlikely(c)) delete c;
      return g;
    }
  if (!c) {
    unlock(); // do not call ctor with lock held
    c = (*ctor)();
    lock();
    goto retry;
  }
  c->m_next = ZmGlobal_list[level];
  ZmGlobal_list[level] = c;
  // printf("ZmGlobal::add() returning new instance %s %p\n", c->m_name, c); fflush(stdout);
  unlock();
  return c;
}

#ifdef ZDEBUG
void ZmGlobal::dump(ZuVStream &s)
{
  lock();
  for (unsigned i = 0; i < ZmCleanup::N; i++)
    for (ZmGlobal *g = ZmGlobal_list[i]; g; g = g->m_next)
      s << g->m_name << ' ' << ZuBoxPtr(g).hex() << '\n';
  unlock();
}
#endif
