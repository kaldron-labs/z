//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

/* disabled lock */

#ifndef ZmNoLock_HH
#define ZmNoLock_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmLockTraits.hh>

class ZmNoLock {
public:
  ZuInline ZmNoLock() { }

  ZmNoLock(const ZmNoLock &) = delete;
  ZmNoLock &operator =(const ZmNoLock &) = delete;

  ZuInline void lock() { }
  ZuInline int trylock() { return 0; }
  ZuInline void unlock() { }

  // ZmCondition integration
  struct Wait { };
  Wait wait() { return {}; }
  ZuInline void lock_() { };
  ZuInline void unlock_() { };
};

template <>
struct ZmLockTraits<ZmNoLock> : public ZmGenericLockTraits<ZmNoLock> {
  ZuInline static void lock(ZmNoLock &l) { }
  ZuInline static int trylock(ZmNoLock &l) { return 0; }
  ZuInline static void unlock(ZmNoLock &l) { }
  ZuInline static void readlock(ZmNoLock &l) { }
  ZuInline static int readtrylock(ZmNoLock &l) { return 0; }
  ZuInline static void readunlock(ZmNoLock &l) { }
};

template <class Lock> class ZmGuard;
template <class Lock> class ZmReadGuard;
template <> class ZmGuard<ZmNoLock> {
public:
  ZuInline ZmGuard() { }
  ZuInline ZmGuard(ZmNoLock &) { }
  ZuInline ZmGuard(const ZmGuard &guard) { }
  ZuInline ~ZmGuard() { }

  ZuInline void unlock() { }

  ZuInline ZmGuard &operator =(const ZmGuard &guard) { return *this; }
};

template <> class ZmReadGuard<ZmNoLock> {
public:
  ZuInline ZmReadGuard() { }
  ZuInline ZmReadGuard(const ZmNoLock &) { }
  ZuInline ZmReadGuard(const ZmReadGuard &guard) { }
  ZuInline ~ZmReadGuard() { }

  ZuInline void unlock() { }

  ZuInline ZmReadGuard &operator =(const ZmReadGuard &guard) { return *this; }
};

#endif /* ZmNoLock_HH */
