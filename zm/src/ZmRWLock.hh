//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// R/W lock

#ifndef ZmRWLock_HH
#define ZmRWLock_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuArray.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmLockTraits.hh>

#if defined(ZDEBUG) && !defined(ZmRWLock_DEBUG)
#define ZmRWLock_DEBUG	// enable testing / debugging
#endif

#ifndef _WIN32
#include <ck_rwlock.h>
#include <ck_pflock.h>

class ZmRWLock {
public:
  ZuInline ZmRWLock() { memset(&m_lock, 0, sizeof(ck_rwlock_recursive_t)); }

  ZmRWLock(const ZmRWLock &) = delete;
  ZmRWLock &operator =(const ZmRWLock &) = delete;

  ZuInline void lock() {
    ck_rwlock_recursive_write_lock(&m_lock, Zm::getTID());
  }
  ZuInline int trylock() {
    return ck_rwlock_recursive_write_trylock(
	&m_lock, Zm::getTID()) ? 0 : -1;
  }
  ZuInline void unlock() {
    ck_rwlock_recursive_write_unlock(&m_lock);
  }

  ZuInline void readlock() {
    ck_rwlock_recursive_read_lock(&m_lock);
  }
  ZuInline int readtrylock() {
    return ck_rwlock_recursive_read_trylock(&m_lock) ? 0 : -1;
  } 
  ZuInline void readunlock() {
    ck_rwlock_recursive_read_unlock(&m_lock);
  }

  template <typename S> void print(S &s) const {
    s << "writer=" << ZuBoxed(m_lock.rw.writer) <<
      " n_readers=" << ZuBoxed(m_lock.rw.n_readers) <<
      " wc=" << ZuBoxed(m_lock.wc);
  }
  friend ZuPrintFn ZuPrintType(ZmRWLock *);

private:
  ck_rwlock_recursive_t	m_lock;
};

template <>
struct ZmLockTraits<ZmRWLock> : public ZmGenericLockTraits<ZmRWLock> {
  enum { RWLock = 1 };
  ZuInline static void readlock(ZmRWLock &l) { l.readlock(); }
  ZuInline static int readtrylock(ZmRWLock &l) { return l.readtrylock(); }
  ZuInline static void readunlock(ZmRWLock &l) { l.readunlock(); }
};

class ZmPRWLock {
public:
  ZuInline ZmPRWLock() { ck_pflock_init(&m_lock); }

  ZmPRWLock(const ZmPRWLock &) = delete;
  ZmPRWLock &operator =(const ZmPRWLock &) = delete;

  ZuInline void lock() { ck_pflock_write_lock(&m_lock); }
  ZuInline void unlock() { ck_pflock_write_unlock(&m_lock); }

  ZuInline void readlock() { ck_pflock_read_lock(&m_lock); }
  ZuInline void readunlock() { ck_pflock_read_unlock(&m_lock); }

  template <typename S> void print(S &s) const {
    s << "rin=" << ZuBoxed(m_lock.rin) <<
      " rout=" << ZuBoxed(m_lock.rout) <<
      " win=" << ZuBoxed(m_lock.win) <<
      " lock=" << ZuBoxed(m_lock.wout);
  }
  friend ZuPrintFn ZuPrintType(ZmPRWLock *);

private:
  ck_pflock_t	m_lock;
};

template <>
struct ZmLockTraits<ZmPRWLock> : public ZmGenericLockTraits<ZmPRWLock> {
  enum { CanTry = 0, Recursive = 0, RWLock = 1 };
  ZuInline static void readlock(ZmPRWLock &l) { l.readlock(); }
  ZuInline static void readunlock(ZmPRWLock &l) { l.readunlock(); }
private:
  static int trylock(ZmPRWLock &);
  static int readtrylock(ZmPRWLock &);
};

#else /* !_WIN32 */

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4251)
#endif

#include <zlib/ZmAssert.hh>
#include <zlib/ZmAtomic.hh>

class ZmPRWLock { // non-recursive
public:
  ZmPRWLock() { InitializeSRWLock(&m_lock); }

  ZmPRWLock(const ZmPRWLock &) = delete;
  ZmPRWLock &operator =(const ZmPRWLock &) = delete;

  ZuInline void lock() { AcquireSRWLockExclusive(&m_lock); }
  ZuInline void unlock() { ReleaseSRWLockExclusive(&m_lock); }
  ZuInline int trylock() {
    return TryAcquireSRWLockExclusive(&m_lock) ? 0 : -1;
  }
  ZuInline void readlock() { AcquireSRWLockShared(&m_lock); }
  ZuInline void readunlock() { ReleaseSRWLockShared(&m_lock); }
  ZuInline int readtrylock() {
    return TryAcquireSRWLockShared(&m_lock) ? 0 : -1;
  }

  template <typename S> void print(S &s) const {
    auto ptr = ZuLaunder(reinterpret_cast<const uintptr_t *>(&m_lock));
    s << ZuBoxed(*ptr);
  }
  friend ZuPrintFn ZuPrintType(ZmPRWLock *);

private:
  SRWLOCK			  m_lock;
};

template <>
struct ZmLockTraits<ZmPRWLock> : public ZmGenericLockTraits<ZmPRWLock> {
  enum { Recursive = 0, RWLock = 1 };
  ZuInline static void readlock(ZmPRWLock &l) { l.readlock(); }
  ZuInline static int readtrylock(ZmPRWLock &l) { return l.readtrylock(); }
  ZuInline static void readunlock(ZmPRWLock &l) { l.readunlock(); }
};

// Recursive RWLock

class ZmRWLock : public ZmPRWLock {
public:
  ZmRWLock() : m_tid(0), m_count(0) { }

  ZmRWLock(const ZmRWLock &) = delete;
  ZmRWLock &operator =(const ZmRWLock &) = delete;

  void lock() {
    if (m_tid == Zm::getTID()) {
      ++m_count;
    } else {
      ZmPRWLock::lock();
      m_tid = Zm::getTID();
      ZmAssert(!m_count);
      ++m_count;
    }
  }
  int trylock() {
    if (m_tid == Zm::getTID()) {
      ++m_count;
      return 0;
    } else if (ZmPRWLock::trylock() == 0) {
      m_tid = Zm::getTID();
      ZmAssert(!m_count);
      ++m_count;
      return 0;
    }
    return -1;
  }
  void unlock() {
    if (!--m_count) {
      m_tid = 0;
      ZmPRWLock::unlock();
    }
  }

  ZuInline void readlock() { ZmPRWLock::readlock(); }
  ZuInline int readtrylock() { return ZmPRWLock::readtrylock(); }
  ZuInline void readunlock() { ZmPRWLock::readunlock(); }

  template <typename S> void print(S &s) const {
    ZmPRWLock::print(s);
    s << " tid=" << ZuBoxed(m_tid.load_()) << " count=" << m_count;
  }
  friend ZuPrintFn ZuPrintType(ZmRWLock *);

private:
  ZmAtomic<Zm::ThreadID>  	m_tid;
  int			      	m_count;
};

template <>
struct ZmLockTraits<ZmRWLock> : public ZmGenericLockTraits<ZmRWLock> {
  enum { RWLock = 1 };
  ZuInline static void readlock(ZmRWLock &l) { l.readlock(); }
  ZuInline static int readtrylock(ZmRWLock &l) { return l.readtrylock(); }
  ZuInline static void readunlock(ZmRWLock &l) { l.readunlock(); }
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* !_WIN32 */

#endif /* ZmRWLock_HH */
