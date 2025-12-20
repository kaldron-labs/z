//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// fast platform-native mutex
// - primitive (potentially non-recursive), cannot be used with ZmCondition

#ifndef ZmPLock_HH
#define ZmPLock_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmLockTraits.hh>
#include <zlib/ZmAtomic.hh>

// Linux x86 - uses Nick Piggin's 2008 reference implementation of ticket
// spinlocks for the Linux kernel, see http://lwn.net/Articles/267968/

// Haswell Core i7 @3.6GHz results (-O3 -mtune=haswell, single NUMA package):
// ZmPLock lock+unlock is ~10.67ns uncontended
// pthread lock+unlock is ~15ns uncontended
// ck ticket lock+unlock is ~10.66ns uncontended
// ck FAS lock+unlock is ~9.5ns uncontended
// contended lock+unlock is generally 40-60ns, within which:
//   performance ranking w/ 2 threads: ck FAS, ck ticket, ZmPLock, pthread
//   performance ranking w/ 3 threads: ck FAS, pthread, ck ticket, ZmPLock
// Note: FAS is unfair, risks starvation/livelock
//
// In general, we optimize for the uncontended case, while moderating
// the penalty of the contended case; furthermore the most common use-case
// in Z is assumed to be 2 threads rather than 3+

#ifndef _WIN32
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#ifdef ZDEBUG
#include <valgrind/helgrind.h>
#endif
using ZmPLock_ = uint32_t;
ZuInline void ZmPLock_init_(ZmPLock_ &m) {
  m = 0;

#ifdef ZDEBUG
  VALGRIND_HG_MUTEX_INIT_POST(&m, 0);
#endif
}
#define ZmPLock_init(m) ZmPLock_init_(m)
ZuInline void ZmPLock_final_(ZmPLock_ &m) {
#ifdef ZDEBUG
  VALGRIND_HG_MUTEX_DESTROY_PRE(&m);
#endif
}
#define ZmPLock_final(m) ZmPLock_final_(m)
ZuInline void ZmPLock_lock_(ZmPLock_ &m) {
#ifdef ZDEBUG
  VALGRIND_HG_MUTEX_LOCK_PRE(&m, false);
#endif

  int i = 0x00010000, j;
  __asm__ __volatile__(	"lock; xaddl %0, %1\n\t"
			"movzwl %w0, %2\n\t"
			"shrl $16, %0\n"
			"1:\tcmpl %0, %2\n\t"
			"je 2f\n\t"
			"rep; nop\n\t"
			"movzwl %1, %2\n\t"
			"jmp 1b\n"
			"2:"
			: "+r" (i), "+m" (m), "=&r" (j)
			: : "memory", "cc");

#ifdef ZDEBUG
  VALGRIND_HG_MUTEX_LOCK_POST(&m);
#endif
}
#define ZmPLock_lock(m) ZmPLock_lock_(m)
ZuInline bool ZmPLock_trylock_(ZmPLock_ &m) {
#ifdef ZDEBUG
  VALGRIND_HG_MUTEX_LOCK_PRE(&m, true);
#endif

  int i, j;
  __asm__ __volatile__(	"movl %2,%0\n\t"
			"movl %0,%1\n\t"
			"roll $16, %0\n\t"
			"cmpl %0,%1\n\t"
#ifdef __x86_64__
			"leal 0x00010000(%q0), %1\n\t"
#else
			"leal 0x00010000(%k0), %1\n\t"
#endif
			"jne 1f\n\t"
			"lock; cmpxchgl %1,%2\n"
			"1:\tsete %b1\n\t"
			"movzbl %b1,%0\n\t"
			: "=&a" (i), "=&q" (j), "+m" (m)
			: : "memory", "cc");

#ifdef ZDEBUG
  VALGRIND_HG_MUTEX_LOCK_POST(&m);
#endif

  return !i;
}
#define ZmPLock_trylock(m) ZmPLock_trylock_(m)
ZuInline void ZmPLock_unlock_(ZmPLock_ &m) {
#ifdef ZDEBUG
  VALGRIND_HG_MUTEX_UNLOCK_PRE(&m);
#endif

  __asm__ __volatile__(	"lock; incw %0"
			: "+m" (m)
			: : "memory", "cc");

#ifdef ZDEBUG
  VALGRIND_HG_MUTEX_UNLOCK_POST(&m);
#endif
}
#define ZmPLock_unlock(m) ZmPLock_unlock_(m) 
#else
using ZmPLock_ = pthread_mutex_t;
#define ZmPLock_init(m) pthread_mutex_init(&m, nullptr);
#define ZmPLock_final(m) pthread_mutex_destroy(&m)
#define ZmPLock_lock(m) pthread_mutex_lock(&m)
#define ZmPLock_trylock(m) (!pthread_mutex_trylock(&m))
#define ZmPLock_unlock(m) pthread_mutex_unlock(&m)
#endif
#else
using ZmPLock_ = CRITICAL_SECTION;
#define ZmPLock_init(m) \
  InitializeCriticalSectionAndSpinCount(&m, 0x10000)
#define ZmPLock_final(m) DeleteCriticalSection(&m)
#define ZmPLock_lock(m) EnterCriticalSection(&m)
#define ZmPLock_trylock(m) TryEnterCriticalSection(&m)
#define ZmPLock_unlock(m) LeaveCriticalSection(&m)
#endif

class ZmPLock {
public:
  ZuInline ZmPLock() { ZmPLock_init(m_lock); }
  ZuInline ~ZmPLock() { ZmPLock_final(m_lock); }

  ZmPLock(const ZmPLock &) = delete;
  ZmPLock &operator =(const ZmPLock &) = delete;

  ZuInline void lock() { ZmPLock_lock(m_lock); }
  ZuInline int trylock() { return -!ZmPLock_trylock(m_lock); }
  ZuInline void unlock() { ZmPLock_unlock(m_lock); }

  // ZmCondition integration
  struct Wait { };
  Wait wait() { return {}; }
  ZuInline void lock_() { ZmPLock_lock(m_lock); }
  ZuInline void unlock_() { ZmPLock_unlock(m_lock); }

private:
  ZmPLock_		m_lock;
};

template <>
struct ZmLockTraits<ZmPLock> : public ZmGenericLockTraits<ZmPLock> {
  enum { CanTry = 1, Recursive = 0, RWLock = 0 };
};

#endif /* ZmPLock_HH */
